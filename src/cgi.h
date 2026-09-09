#ifndef CGI_H
#define CGI_H

#include "base.h"
#include <lua.h>

typedef struct {
  lua_State *L;
} CGI;

CGI *cgi_init();
void cgi_shutdown(CGI *cgi);
bool cgi_run(CGI *cgi, Arena *arena, const char *filePath, String *output);

#endif