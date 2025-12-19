#include "editor_internal.h"

#include <ctype.h>
#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void editor_insert_char(Editor *E, int ch) {
    Line *ln = &E->lines[E->cy];
    line_insert_char(ln, E->cx, ch);
    E->cx++;
    E->dirty = true;
}

static void editor_insert_newline(Editor *E) {
    Line *ln = &E->lines[E->cy];

    // split ln at cx
    size_t left_len = E->cx;
    if (left_len > ln->len) left_len = ln->len;
    size_t right_len = ln->len - left_len;

    Line right = line_new_from(ln->data + left_len, right_len);

    ln->len = left_len;
    if (ln->data) ln->data[ln->len] = '\0';

    editor_insert_line(E, E->cy + 1, right);

    E->cy++;
    E->cx = 0;
    E->dirty = true;
}

static void editor_backspace(Editor *E) {
    if (E->cy == 0 && E->cx == 0) return;

    Line *ln = &E->lines[E->cy];
    if (E->cx > 0) {
        line_del_char(ln, E->cx - 1);
        E->cx--;
    } else {
        // merge with previous line
        Line *prev = &E->lines[E->cy - 1];
        size_t old_prev_len = prev->len;
        line_ensure_cap(prev, prev->len + ln->len + 1);
        memcpy(prev->data + prev->len, ln->data, ln->len);
        prev->len += ln->len;
        prev->data[prev->len] = '\0';

        editor_delete_line(E, E->cy);
        E->cy--;
        E->cx = old_prev_len;
    }
    E->dirty = true;
}

static void editor_delete(Editor *E) {
    Line *ln = &E->lines[E->cy];
    if (E->cx < ln->len) {
        line_del_char(ln, E->cx);
        E->dirty = true;
        return;
    }
    // at end: merge with next line
    if (E->cy + 1 >= E->nlines) return;
    Line *next = &E->lines[E->cy + 1];
    line_ensure_cap(ln, ln->len + next->len + 1);
    memcpy(ln->data + ln->len, next->data, next->len);
    ln->len += next->len;
    ln->data[ln->len] = '\0';
    editor_delete_line(E, E->cy + 1);
    E->dirty = true;
}

static void editor_move_cursor(Editor *E, int key) {
    Line *ln = &E->lines[E->cy];

    switch (key) {
        case KEY_LEFT:
            if (E->cx > 0) E->cx--;
            else if (E->cy > 0) {
                E->cy--;
                E->cx = E->lines[E->cy].len;
            }
            break;
        case KEY_RIGHT:
            if (E->cx < ln->len) E->cx++;
            else if (E->cy + 1 < E->nlines) {
                E->cy++;
                E->cx = 0;
            }
            break;
        case KEY_UP:
            if (E->cy > 0) E->cy--;
            break;
        case KEY_DOWN:
            if (E->cy + 1 < E->nlines) E->cy++;
            break;
        case KEY_HOME:
            E->cx = 0;
            break;
        case KEY_END:
            E->cx = E->lines[E->cy].len;
            break;
        case KEY_PPAGE: // Page Up
            for (int i = 0; i < E->screen_rows - 2; i++) editor_move_cursor(E, KEY_UP);
            break;
        case KEY_NPAGE: // Page Down
            for (int i = 0; i < E->screen_rows - 2; i++) editor_move_cursor(E, KEY_DOWN);
            break;
    }

    // clamp cx to line length
    ln = &E->lines[E->cy];
    if (E->cx > ln->len) E->cx = ln->len;
}

static void editor_scroll(Editor *E) {
    int text_rows = E->screen_rows - 2; // last 2 lines: status + message
    if (text_rows < 1) text_rows = 1;

    if (E->cy < E->rowoff) E->rowoff = E->cy;
    if (E->cy >= E->rowoff + (size_t)text_rows) E->rowoff = E->cy - (size_t)text_rows + 1;

    if (E->cx < E->coloff) E->coloff = E->cx;
    if (E->cx >= E->coloff + (size_t)E->screen_cols) E->coloff = E->cx - (size_t)E->screen_cols + 1;
}

void editor_refresh_screen(Editor *E) {
    getmaxyx(stdscr, E->screen_rows, E->screen_cols);
    editor_scroll(E);

    int text_rows = E->screen_rows - 2;
    if (text_rows < 1) text_rows = 1;

    erase();

    // draw text area
    for (int y = 0; y < text_rows; y++) {
        size_t filerow = E->rowoff + (size_t)y;
        move(y, 0);
        clrtoeol();

        if (filerow >= E->nlines) {
            addch('~');
            continue;
        }

        Line *ln = &E->lines[filerow];
        if (E->coloff < ln->len) {
            size_t avail = (size_t)E->screen_cols;
            size_t to_print = ln->len - E->coloff;
            if (to_print > avail) to_print = avail;
            // Print visible part
            addnstr(ln->data + E->coloff, (int)to_print);
        }
    }

    // status bar
    attron(A_REVERSE);
    char status[256];
    char rstatus[64];
    const char *name = E->filename ? E->filename : "[No Name]";
    snprintf(status, sizeof(status), " %s%s", name, E->dirty ? " (modified)" : "");
    snprintf(rstatus, sizeof(rstatus), " Ln %zu, Col %zu ", E->cy + 1, E->cx + 1);

    int y_status = E->screen_rows - 2;
    move(y_status, 0);
    clrtoeol();
    addnstr(status, E->screen_cols);

    // right-aligned rstatus
    int rlen = (int)strlen(rstatus);
    int col = E->screen_cols - rlen;
    if (col < 0) col = 0;
    mvaddnstr(y_status, col, rstatus, E->screen_cols - col);
    attroff(A_REVERSE);

    // message line
    int y_msg = E->screen_rows - 1;
    move(y_msg, 0);
    clrtoeol();
    if (E->msg[0]) addnstr(E->msg, E->screen_cols);

    // place cursor
    int cx_screen = (int)(E->cx - E->coloff);
    int cy_screen = (int)(E->cy - E->rowoff);
    if (cy_screen < 0) cy_screen = 0;
    if (cy_screen >= text_rows) cy_screen = text_rows - 1;
    if (cx_screen < 0) cx_screen = 0;
    if (cx_screen >= E->screen_cols) cx_screen = E->screen_cols - 1;

    move(cy_screen, cx_screen);
    refresh();
}

static bool editor_confirm_quit(Editor *E) {
    if (!E->dirty) return true;
    editor_set_msg(E, "Unsaved changes! Press Ctrl+Q again to quit, or Ctrl+S to save.");
    editor_refresh_screen(E);
    int c = getch();
    if (c == 17) return true; // Ctrl+Q
    if (c == 19) { editor_save(E); return false; } // Ctrl+S
    editor_set_msg(E, "");
    return false;
}

void editor_process_key(Editor *E, int c) {
    // Ctrl keys: c & 0x1f
    if (c == 17) { // Ctrl+Q
        if (editor_confirm_quit(E)) {
            endwin();
            exit(0);
        }
        return;
    }
    if (c == 19) { // Ctrl+S
        editor_save(E);
        return;
    }

    switch (c) {
        case KEY_UP:
        case KEY_DOWN:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_HOME:
        case KEY_END:
        case KEY_PPAGE:
        case KEY_NPAGE:
            editor_move_cursor(E, c);
            break;
        case KEY_BACKSPACE:
        case 127:
        case 8:
            editor_backspace(E);
            break;
        case KEY_DC:
            editor_delete(E);
            break;
        case '\r':
        case '\n':
            editor_insert_newline(E);
            break;
        case KEY_RESIZE:
            // handled in refresh by getmaxyx()
            break;
        default:
            if (isprint(c) || c == '\t') {
                editor_insert_char(E, c);
            }
            break;
    }
}

void editor_init(Editor *E, const char *filename) {
    memset(E, 0, sizeof(*E));
    E->filename = filename ? xstrdup(filename) : NULL;
    E->lines = NULL;
    E->nlines = 0;
    E->cap = 0;
    editor_insert_line(E, 0, line_new_from("", 0));
    editor_set_msg(E, "Ctrl+S save | Ctrl+Q quit");

    if (E->filename) editor_load_file(E, E->filename);
}

void editor_free(Editor *E) {
    for (size_t i = 0; i < E->nlines; i++) line_free(&E->lines[i]);
    free(E->lines);
    free(E->filename);
}
