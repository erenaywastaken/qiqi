/*
    jwaxy's extension to base.h
*/

#include "base.h"

#define STR_FMT "%.*s"
#define STR_ARG(s) (int)(s).length, (s).data

#define _GRAYISH "\x1b[0;37m"

typedef struct {
  String str;
  String delimiter;
  size_t offset;
} SplitView;

SplitView StrSplitView(String str, String delimiter);
String SplitViewNext(SplitView *split);
String PathJoin(Arena *arena, String base, String path);
FileReadResult FileReadEz(Arena *arena, String path);

#ifdef NDEBUG
#define LogDebug(...) ((void)0)
#else
void LogDebug(const char *format, ...) FORMAT_CHECK(1, 2);
#endif

#ifdef BASE_IMPLEMENTATION

#ifndef NDEBUG
void LogDebug(const char *format, ...) {
  printf("%s[DEBUG]: ", _GRAYISH);
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("%s\n", _RESET);
}
#endif

SplitView StrSplitView(String str, String delimiter) {
  return (SplitView){
      .str = str,
      .delimiter = delimiter,
      .offset = 0,
  };
}

String SplitViewNext(SplitView *split) {
  if (split->offset > split->str.length)
    return (String){0};

  size_t start = split->offset;

  while (split->offset + split->delimiter.length <= split->str.length) {
    if (memcmp(split->str.data + split->offset, split->delimiter.data,
               split->delimiter.length) == 0) {
      String result = {
          .length = split->offset - start,
          .data = split->str.data + start,
      };

      split->offset += split->delimiter.length;
      return result;
    }

    ++split->offset;
  }

  String result = {
      .length = split->str.length - start,
      .data = split->str.data + start,
  };

  split->offset = split->str.length + 1;

  return result;
}

String PathJoin(Arena *arena, String base, String path) {
  if (StrIsEmpty(base)) {
    return StrNew(arena, path.data);
  }

#if defined(BASE_PLATFORM_WIN)
  String separator = S("\\");
#else
  String separator = S("/");
#endif

  bool base_has_sep = false;
  if (base.length > 0) {
    char last_char = base.data[base.length - 1];
#if defined(BASE_PLATFORM_WIN)
    base_has_sep = (last_char == '\\' || last_char == '/');
#else
    base_has_sep = (last_char == '/');
#endif
  }

  bool path_has_leading_sep = (path.length > 0 && path.data[0] == '/');

  String result;
  if (base_has_sep && path_has_leading_sep) {
    result = StrConcat(arena, base, StrSlice(arena, path, 1, -1));
  } else if (!base_has_sep && !path_has_leading_sep) {
    result = StrConcat(arena, StrConcat(arena, base, separator), path);
  } else {
    result = StrConcat(arena, base, path);
  }

  return result;
}

FileReadResult FileReadEz(Arena *arena, String filePath) {
  FileStatsResult stats = FileStats(filePath);
  if (stats.error != SUCCESS) {
    return (FileReadResult){.error = stats.error};
  }

  FileReadResult content = FileRead(arena, filePath, stats.data.size);
  return content;
}

#endif