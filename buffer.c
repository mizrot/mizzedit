#include "editor_internal.h"

#include <string.h>

void editor_ensure_lines(Editor *E, size_t need) {
    if (need <= E->cap) return;
    size_t newcap = E->cap ? E->cap : 32;
    while (newcap < need) newcap *= 2;
    E->lines = xrealloc(E->lines, newcap * sizeof(Line));
    E->cap = newcap;
}

void editor_insert_line(Editor *E, size_t at, Line ln) {
    if (at > E->nlines) at = E->nlines;
    editor_ensure_lines(E, E->nlines + 1);
    memmove(&E->lines[at + 1], &E->lines[at], (E->nlines - at) * sizeof(Line));
    E->lines[at] = ln;
    E->nlines++;
}

void editor_delete_line(Editor *E, size_t at) {
    if (at >= E->nlines) return;
    line_free(&E->lines[at]);
    memmove(&E->lines[at], &E->lines[at + 1], (E->nlines - at - 1) * sizeof(Line));
    E->nlines--;
    if (E->nlines == 0) {
        editor_insert_line(E, 0, line_new_from("", 0));
    }
}
