#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

void die(const char *what);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

#endif
