// miedit.c — minimal ncurses text editor
// Build: cc -std=c11 -Wall -Wextra -pedantic -O2 miedit.c -lncurses -o miedit
// Usage: ./miedit [file]

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Line;

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

static void die(const char *what) {
    endwin();
    fprintf(stderr, "%s: %s\n", what, strerror(errno));
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) die("malloc");
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) die("realloc");
    return q;
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---------- Line helpers ---------- */

static void line_ensure_cap(Line *ln, size_t need) {
    if (need <= ln->cap) return;
    size_t newcap = ln->cap ? ln->cap : 16;
    while (newcap < need) newcap *= 2;
    ln->data = xrealloc(ln->data, newcap);
    ln->cap = newcap;
}

static Line line_new_from(const char *s, size_t len) {
    Line ln = {0};
    line_ensure_cap(&ln, len + 1);
    if (len) memcpy(ln.data, s, len);
    ln.len = len;
    ln.data[ln.len] = '\0';
    return ln;
}

static void line_free(Line *ln) {
    free(ln->data);
    ln->data = NULL;
    ln->len = ln->cap = 0;
}

static void line_insert_char(Line *ln, size_t at, int ch) {
    if (at > ln->len) at = ln->len;
    line_ensure_cap(ln, ln->len + 2);
    memmove(&ln->data[at + 1], &ln->data[at], ln->len - at + 1);
    ln->data[at] = (char)ch;
    ln->len++;
}

static void line_del_char(Line *ln, size_t at) {
    if (ln->len == 0 || at >= ln->len) return;
    memmove(&ln->data[at], &ln->data[at + 1], ln->len - at);
    ln->len--;
}

/* ---------- Document helpers ---------- */

static void editor_ensure_lines(Editor *E, size_t need) {
    if (need <= E->cap) return;
    size_t newcap = E->cap ? E->cap : 32;
    while (newcap < need) newcap *= 2;
    E->lines = xrealloc(E->lines, newcap * sizeof(Line));
    E->cap = newcap;
}

static void editor_insert_line(Editor *E, size_t at, Line ln) {
    if (at > E->nlines) at = E->nlines;
    editor_ensure_lines(E, E->nlines + 1);
    memmove(&E->lines[at + 1], &E->lines[at], (E->nlines - at) * sizeof(Line));
    E->lines[at] = ln;
    E->nlines++;
}

static void editor_delete_line(Editor *E, size_t at) {
    if (at >= E->nlines) return;
    line_free(&E->lines[at]);
    memmove(&E->lines[at], &E->lines[at + 1], (E->nlines - at - 1) * sizeof(Line));
    E->nlines--;
    if (E->nlines == 0) {
        editor_insert_line(E, 0, line_new_from("", 0));
    }
}

/* ---------- File I/O ---------- */

static void editor_set_msg(Editor *E, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E->msg, sizeof(E->msg), fmt, ap);
    va_end(ap);
}

static void editor_load_file(Editor *E, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        // New file is okay
        editor_set_msg(E, "New file: %s (Ctrl+S to save)", path);
        return;
    }

    // Clear existing
    for (size_t i = 0; i < E->nlines; i++) line_free(&E->lines[i]);
    E->nlines = 0;

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, f)) != -1) {
        // Strip trailing \n / \r\n
        size_t len = (size_t)n;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
        editor_insert_line(E, E->nlines, line_new_from(line, len));
    }
    free(line);
    fclose(f);

    if (E->nlines == 0) editor_insert_line(E, 0, line_new_from("", 0));
    E->dirty = false;
    editor_set_msg(E, "Opened: %s", path);
}

static bool editor_save(Editor *E) {
    if (!E->filename || !E->filename[0]) {
        editor_set_msg(E, "No filename. Run as: ./miedit file.txt");
        return false;
    }

    // Write to temp then rename (best effort).
    size_t tmpsz = strlen(E->filename) + 16;
    char *tmp = xmalloc(tmpsz);
    snprintf(tmp, tmpsz, "%s.tmp", E->filename);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        editor_set_msg(E, "Save failed: %s", strerror(errno));
        free(tmp);
        return false;
    }

    for (size_t i = 0; i < E->nlines; i++) {
        if (E->lines[i].len && fwrite(E->lines[i].data, 1, E->lines[i].len, f) != E->lines[i].len) {
            editor_set_msg(E, "Write failed");
            fclose(f);
            remove(tmp);
            free(tmp);
            return false;
        }
        if (fputc('\n', f) == EOF) {
            editor_set_msg(E, "Write failed");
            fclose(f);
            remove(tmp);
            free(tmp);
            return false;
        }
    }

    if (fclose(f) != 0) {
        editor_set_msg(E, "Close failed: %s", strerror(errno));
        remove(tmp);
        free(tmp);
        return false;
    }

    if (rename(tmp, E->filename) != 0) {
        editor_set_msg(E, "Rename failed: %s", strerror(errno));
        remove(tmp);
        free(tmp);
        return false;
    }

    free(tmp);
    E->dirty = false;
    editor_set_msg(E, "Saved: %s", E->filename);
    return true;
}

/* ---------- Editing operations ---------- */

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

/* ---------- Movement / scrolling ---------- */

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

/* ---------- Rendering ---------- */

static void editor_refresh_screen(Editor *E) {
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

/* ---------- Input loop ---------- */

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

static void editor_process_key(Editor *E, int c) {
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

/* ---------- Init / cleanup ---------- */

static void editor_init(Editor *E, const char *filename) {
    memset(E, 0, sizeof(*E));
    E->filename = filename ? xstrdup(filename) : NULL;
    E->lines = NULL;
    E->nlines = 0;
    E->cap = 0;
    editor_insert_line(E, 0, line_new_from("", 0));
    editor_set_msg(E, "Ctrl+S save | Ctrl+Q quit");

    if (E->filename) editor_load_file(E, E->filename);
}

static void editor_free(Editor *E) {
    for (size_t i = 0; i < E->nlines; i++) line_free(&E->lines[i]);
    free(E->lines);
    free(E->filename);
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");

    Editor E;
    editor_init(&E, argc >= 2 ? argv[1] : NULL);

    initscr();
    raw();               // raw mode (Ctrl+Z etc. handled by us)
    noecho();            // don't echo keys
    keypad(stdscr, TRUE);
    set_escdelay(25);
    curs_set(1);

    // Use terminal default colors if supported
    if (has_colors()) {
        start_color();
        use_default_colors();
    }

    while (1) {
        editor_refresh_screen(&E);
        int c = getch();
        editor_process_key(&E, c);
    }

    // unreachable
    editor_free(&E);
    endwin();
    return 0;
}

