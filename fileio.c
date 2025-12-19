#define _POSIX_C_SOURCE 200809L
#include "editor_internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

void editor_set_msg(Editor *E, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E->msg, sizeof(E->msg), fmt, ap);
    va_end(ap);
}

void editor_load_file(Editor *E, const char *path) {
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

bool editor_save(Editor *E) {
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
