/*
    jwaxy's extension to base.h
*/

#include "base.h"

#define STR_FMT "%.*s"
#define STR_ARG(s) (int)(s).length, (s).data

#define _GRAYISH "\x1b[0;37m"

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

#endif