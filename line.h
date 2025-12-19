#ifndef LINE_H
#define LINE_H

#include <stddef.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Line;

void line_ensure_cap(Line *ln, size_t need);
Line line_new_from(const char *s, size_t len);
void line_free(Line *ln);
void line_insert_char(Line *ln, size_t at, int ch);
void line_del_char(Line *ln, size_t at);

#endif
