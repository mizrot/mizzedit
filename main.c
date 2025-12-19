#include "editor.h"

#include <locale.h>
#include <ncurses.h>

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
