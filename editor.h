#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <stddef.h>

#include "line.h"

typedef struct {
    Line  *lines;
    size_t nlines;
    size_t cap;

    // cursor position (in document coordinates)
    size_t cy; // line index
    size_t cx; // column within line (0..len)

    // viewport (top-left) in document coordinates
    size_t rowoff; // first visible line
    size_t coloff; // first visible column

    // screen size
    int screen_rows; // includes status/message line; we render text into screen_rows-2
    int screen_cols;

    // file + state
    char *filename;
    bool dirty;

    // message line
    char msg[256];
} Editor;

void editor_init(Editor *E, const char *filename);
void editor_free(Editor *E);
void editor_refresh_screen(Editor *E);
void editor_process_key(Editor *E, int c);

#endif
