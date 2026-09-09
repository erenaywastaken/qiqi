#define BASE_IMPLEMENTATION
#include "base.h"
#include "base_ext.h"

typedef enum { HTML, LUA_EXPR, LUA_BLOCK } State;

static void emitSlice(StringBuilder *builder, String prefix, char *start,
                      size_t length, String suffix) {
  SBAdd(builder, prefix);
  SBAdd(builder, (String){.data = start, .length = length});
  SBAdd(builder, suffix);
}

static bool isStartToken(String t) {
  return StrEq(t, S("do")) || StrEq(t, S("then")) || StrEq(t, S("else")) ||
         StrEq(t, S("return"));
}

static bool transpile(StringBuilder *builder, String input) {
  State state = LUA_BLOCK;
  char *sliceStart = input.data;
  char *luaTokenStart = input.data;
  String prevLuaToken = S("");
  bool atStmtStart = true;
  int htmlDepth = 0;
  bool htmlClosingTag = false;
  char *end = input.data + input.length;

  for (size_t i = 0; i < input.length; ++i) {
    char c = input.data[i];

    switch (state) {
    case LUA_BLOCK: {
      if (c == '<') {
        if (atStmtStart || StrEq(prevLuaToken, S("("))) {
          size_t len = input.data + i - sliceStart;
          emitSlice(builder, S(""), sliceStart, len, S(""));

          state = HTML;
          sliceStart = input.data + i;
          ++htmlDepth;
        }
      } else if (c == ';' || c == '\n') {
        size_t len = input.data + i - luaTokenStart;
        if (len > 0)
          prevLuaToken = (String){.data = luaTokenStart, .length = len};
        luaTokenStart = input.data + i + 1;
        atStmtStart = true;
      } else if (c == ' ' || c == '\r' || c == '\t' || c == '(') {
        size_t len = input.data + i - luaTokenStart;
        if (len > 0) {
          prevLuaToken = (String){.data = luaTokenStart, .length = len};
          atStmtStart = isStartToken(prevLuaToken);
        }
        if (c == '(')
          prevLuaToken = S("(");
        luaTokenStart = input.data + i + 1;
      }
    } break;

    case HTML: {
      if (c == '{' && i + 1 < input.length && input.data[i + 1] == '{') {
        size_t len = input.data + i - sliceStart;
        emitSlice(builder, S("write([["), sliceStart, len, S("]])\n"));

        state = LUA_EXPR;
        sliceStart = input.data + i + 2;
        ++i;
      } else if (c == '<') {
        htmlClosingTag = i + 1 < input.length && input.data[i + 1] == '/';

        if (!htmlClosingTag)
          ++htmlDepth;
      } else if (c == '>' && htmlClosingTag) {
        --htmlDepth;

        if (htmlDepth == 0) {
          size_t len = input.data + i + 1 - sliceStart;
          emitSlice(builder, S("write([["), sliceStart, len, S("]])\n"));

          state = LUA_BLOCK;
          sliceStart = input.data + i + 1;
        }

        htmlClosingTag = false;
      } else if (c == '>' && !htmlClosingTag) {
        // Self closing tag < />
        if (i > 0 && input.data[i - 1] == '/')
          --htmlDepth;
      }
    } break;

    case LUA_EXPR: {
      if (c == '}' && i + 1 < input.length && input.data[i + 1] == '}') {
        size_t len = input.data + i - sliceStart;
        emitSlice(builder, S("write("), sliceStart, len, S(")\n"));

        state = HTML;
        sliceStart = input.data + i + 2;
        ++i;
      }
    } break;
    }
  }

  // Flush whatever's left
  size_t remaining = end - sliceStart;
  if (state == HTML && remaining > 0) {
    emitSlice(builder, S("write([["), sliceStart, remaining, S("]])\n"));
  } else if (state == LUA_BLOCK && remaining > 0) {
    emitSlice(builder, S(""), sliceStart, remaining, S(""));
  } else if (state == LUA_EXPR) {
    LogError("Unterminated Lua expression at End-Of-File");
    return false;
  }

  return true;
}

int main(int argc, char *argv[]) {
  LogInit();

  if (argc != 3) {
    LogError("Usage: ./transpiler input.ljsx output.lua");
    return 1;
  }

  String inputPath = s(argv[1]);
  String outputPath = s(argv[2]);

  Arena *arena = ArenaCreate(1024);

  FileReadResult input = FileReadEz(arena, inputPath);
  if (input.error != SUCCESS) {
    String err = ErrToStr(input.error);
    LogError("Couldn't read input file '" STR_FMT "': " STR_FMT,
             STR_ARG(inputPath), STR_ARG(err));
    return 1;
  }

  StringBuilder builder = SBCreate(arena);

  if (!transpile(&builder, input.data)) {
    return 1;
  }

  Error error = FileWrite(outputPath, builder.buffer);
  if (error != SUCCESS) {
    String errorStr = ErrToStr(error);
    LogError("Couldn't write to output path '" STR_FMT "': " STR_FMT,
             STR_ARG(outputPath), STR_ARG(errorStr));
    return 1;
  }

  LogInfo("Done!");
  return 0;
}
