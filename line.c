#include "line.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void line_ensure_cap(Line *ln, size_t need) {
    if (need <= ln->cap) return;
    size_t newcap = ln->cap ? ln->cap : 16;
    while (newcap < need) newcap *= 2;
    ln->data = xrealloc(ln->data, newcap);
    ln->cap = newcap;
}

Line line_new_from(const char *s, size_t len) {
    Line ln = {0};
    line_ensure_cap(&ln, len + 1);
    if (len) memcpy(ln.data, s, len);
    ln.len = len;
    ln.data[ln.len] = '\0';
    return ln;
}

void line_free(Line *ln) {
    free(ln->data);
    ln->data = NULL;
    ln->len = ln->cap = 0;
}

void line_insert_char(Line *ln, size_t at, int ch) {
    if (at > ln->len) at = ln->len;
    line_ensure_cap(ln, ln->len + 2);
    memmove(&ln->data[at + 1], &ln->data[at], ln->len - at + 1);
    ln->data[at] = (char)ch;
    ln->len++;
}

void line_del_char(Line *ln, size_t at) {
    if (ln->len == 0 || at >= ln->len) return;
    memmove(&ln->data[at], &ln->data[at + 1], ln->len - at);
    ln->len--;
}
