#ifndef EDITOR_INTERNAL_H
#define EDITOR_INTERNAL_H

#include "editor.h"
#include "util.h"

void editor_set_msg(Editor *E, const char *fmt, ...);
void editor_load_file(Editor *E, const char *path);
bool editor_save(Editor *E);

void editor_ensure_lines(Editor *E, size_t need);
void editor_insert_line(Editor *E, size_t at, Line ln);
void editor_delete_line(Editor *E, size_t at);

#endif
