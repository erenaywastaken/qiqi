#include "cgi.h"
#include "base_ext.h"
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>

static int l_write(lua_State *L) {
  if (lua_isnoneornil(L, 1))
    return 0;

  StringBuilder *buf = (StringBuilder *)lua_touserdata(L, lua_upvalueindex(1));
  if (buf == NULL) {
    return luaL_error(L, "Error: Write buffer not found!");
  }

  size_t len;
  const char *s = lua_tolstring(L, 1, &len);

  if (s != NULL) {
    SBAdd(buf, (String){.data = (char *)s, .length = len});
  }

  return 0;
}

CGI *cgi_init() {
  CGI *cgi = Malloc(sizeof(CGI));
  cgi->L = luaL_newstate();
  luaL_openlibs(cgi->L);
  return cgi;
}

void cgi_shutdown(CGI *cgi) {
  lua_close(cgi->L);
  Free(cgi);
}

// Output contains the error message if this returns false
bool cgi_run(CGI *cgi, Arena *arena, const char *filePath, String *output) {
  StringBuilder builder = SBCreate(arena);

  lua_pushlightuserdata(cgi->L, &builder);
  lua_pushcclosure(cgi->L, l_write, 1);
  lua_setglobal(cgi->L, "write");

  if (luaL_dofile(cgi->L, filePath) != LUA_OK) {
    // TODO: Is this safe? how long does the return of lua_tolstring live?
    size_t len = 0;
    const char *errMsg = lua_tolstring(cgi->L, -1, &len);
    *output = (String){len, (char *)errMsg};
    lua_pop(cgi->L, 1);
    return false;
  }

  lua_getglobal(cgi->L, "Index");

  if (lua_isnoneornil(cgi->L, -1)) {
    *output = S("Error: Index isn't defined (hint: don't use local)\n");
    lua_pop(cgi->L, 1);
    return false;
  }

  if (!lua_isfunction(cgi->L, -1)) {
    *output = S("Error: Index is not a function\n");
    lua_pop(cgi->L, 1);
    return false;
  }

  // Call Index().
  if (lua_pcall(cgi->L, 0, 0, 0) != LUA_OK) {
    size_t len = 0;
    const char *errMsg = lua_tolstring(cgi->L, -1, &len);
    String errStr = (String){len, (char *)errMsg};

    StringBuilder errBuilder = SBCreate(arena);
    SBAddF(&errBuilder, "Error calling Index: %S", errStr);
    *output = errBuilder.buffer;

    lua_pop(cgi->L, 1);
    return false;
  }

  *output = builder.buffer;
  return true;
}