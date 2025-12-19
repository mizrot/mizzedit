#include <bits/stdc++.h>
#include <ncurses.h>
using namespace std;

static const int INF = 1e9;

static void commitFrame(WINDOW* wLeft, WINDOW* wRight, WINDOW* wOut) {
    // IMPORTANT: stage all windows, then one doupdate()
    wnoutrefresh(stdscr);
    wnoutrefresh(wLeft);
    wnoutrefresh(wRight);
    wnoutrefresh(wOut);
    doupdate();
}


enum PrevBits : uint8_t { PREV_NONE=0, PREV_M=1<<0, PREV_D=1<<1, PREV_I=1<<2 };
enum OpType { OP_M, OP_D, OP_I };

struct Op {
    OpType t;
    char ch;
};

struct Transition {
    int pi, pj;
    Op op;
};

static int opRank(OpType t) {
    switch (t) {
        case OP_M: return 0;
        case OP_D: return 1;
        case OP_I: return 2;
    }
    return 3;
}

static void printColoredOps(WINDOW* w, int y, int x, const vector<Op>& ops, int maxWidth) {
    int cx = x;
    for (const auto& op : ops) {
        if (cx >= x + maxWidth) break;

        if (op.t == OP_M) {
            wattron(w, COLOR_PAIR(1));
            mvwaddch(w, y, cx++, op.ch);
            wattroff(w, COLOR_PAIR(1));
        } else if (op.t == OP_D) {
            wattron(w, COLOR_PAIR(2) | A_DIM);
            mvwaddch(w, y, cx++, op.ch);
            wattroff(w, COLOR_PAIR(2) | A_DIM);
        } else { // OP_I
            wattron(w, COLOR_PAIR(3) | A_BOLD | A_UNDERLINE);
            mvwaddch(w, y, cx++, op.ch);
            wattroff(w, COLOR_PAIR(3) | A_BOLD | A_UNDERLINE);
        }
    }
}

static void drawBoxWithTitle(WINDOW* w, const string& title) {
    werase(w);
    box(w, 0, 0);
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 2, " %s ", title.c_str());
    wattroff(w, A_BOLD);
}

static void drawInput(WINDOW* w, const string& s, bool active) {
    int h, wdt;
    getmaxyx(w, h, wdt);

    int usable = max(0, wdt - 4);
    string view = s;
    if ((int)view.size() > usable) view = view.substr(view.size() - usable);

    if (active) wattron(w, A_REVERSE);
    mvwprintw(w, 1, 2, "%-*s", usable, view.c_str());
    if (active) wattroff(w, A_REVERSE);

    int cx = 2 + min((int)view.size(), usable);
    wmove(w, 1, cx);
}

struct DPBundle {
    int dist = 0;
    // transitions into (i,j): list of optimal parents with op, sorted deterministically
    vector<vector<vector<Transition>>> trans; // (n+1) x (m+1)
};

static DPBundle computeDPWithTransitions(const string& S, const string& T) {
    int n = (int)S.size(), m = (int)T.size();
    vector<vector<int>> dp(n+1, vector<int>(m+1, INF));
    vector<vector<uint8_t>> prev(n+1, vector<uint8_t>(m+1, PREV_NONE));

    dp[0][0] = 0;
    for (int i = 1; i <= n; i++) { dp[i][0] = i; prev[i][0] = PREV_D; }
    for (int j = 1; j <= m; j++) { dp[0][j] = j; prev[0][j] = PREV_I; }

    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            int best = INF;
            uint8_t mask = PREV_NONE;

            int vD = dp[i-1][j] + 1;
            if (vD < best) { best = vD; mask = PREV_D; }
            else if (vD == best) mask |= PREV_D;

            int vI = dp[i][j-1] + 1;
            if (vI < best) { best = vI; mask = PREV_I; }
            else if (vI == best) mask |= PREV_I;

            if (S[i-1] == T[j-1]) {
                int vM = dp[i-1][j-1];
                if (vM < best) { best = vM; mask = PREV_M; }
                else if (vM == best) mask |= PREV_M;
            }

            dp[i][j] = best;
            prev[i][j] = mask;
        }
    }

    // Build transitions matrix
    vector<vector<vector<Transition>>> trans(n+1, vector<vector<Transition>>(m+1));
    for (int i = 0; i <= n; i++) {
        for (int j = 0; j <= m; j++) {
            uint8_t mask = prev[i][j];
            vector<Transition> tlist;
            tlist.reserve(3);

            if ((mask & PREV_M) && i>0 && j>0) tlist.push_back({i-1, j-1, {OP_M, S[i-1]}});
            if ((mask & PREV_D) && i>0)        tlist.push_back({i-1, j,   {OP_D, S[i-1]}});
            if ((mask & PREV_I) && j>0)        tlist.push_back({i,   j-1, {OP_I, T[j-1]}});

            sort(tlist.begin(), tlist.end(), [](const Transition& a, const Transition& b){
                int ra = opRank(a.op.t), rb = opRank(b.op.t);
                if (ra != rb) return ra < rb;
                unsigned char ca = (unsigned char)a.op.ch;
                unsigned char cb = (unsigned char)b.op.ch;
                if (ca != cb) return ca < cb;
                if (a.pi != b.pi) return a.pi < b.pi;
                return a.pj < b.pj;
            });

            trans[i][j] = std::move(tlist);
        }
    }

    DPBundle out;
    out.dist = dp[n][m];
    out.trans = std::move(trans);
    return out;
}

// --------- Lazy iterator over ALL minimal histories (DFS in deterministic order) ---------
class HistoryIterator {
public:
    HistoryIterator() = default;

    void reset(const DPBundle* bundle, int n, int m) {
        b = bundle;
        N = n; M = m;
        finished = false;
        opsRev.clear();
        stack.clear();

        if (!b) { finished = true; return; }
        stack.push_back(Frame{N, M, 0});
        // Note: transitions are in b->trans[i][j]
    }

    // returns false when exhausted
    bool next(vector<Op>& outOps) {
        outOps.clear();
        if (finished || !b) return false;

        while (!stack.empty()) {
            Frame &f = stack.back();

            if (f.i == 0 && f.j == 0) {
                // yield current history (opsRev is path from (N,M)->(0,0))
                vector<Op> ops = opsRev;
                reverse(ops.begin(), ops.end());
                outOps = std::move(ops);

                // backtrack: pop (0,0) frame
                stack.pop_back();
                if (!opsRev.empty()) opsRev.pop_back();
                return true;
            }

            const auto& tlist = b->trans[f.i][f.j];
            if (f.k >= tlist.size()) {
                // no more children from this frame -> backtrack
                stack.pop_back();
                if (!opsRev.empty()) opsRev.pop_back();
                continue;
            }

            // Take next transition
            const Transition& tr = tlist[f.k++];
            opsRev.push_back(tr.op);
            stack.push_back(Frame{tr.pi, tr.pj, 0});
        }

        finished = true;
        return false;
    }

    bool done() const { return finished; }

private:
    struct Frame { int i, j; size_t k; };

    const DPBundle* b = nullptr;
    int N = 0, M = 0;
    bool finished = true;

    vector<Frame> stack;
    vector<Op> opsRev;
};

// ---------- UI ----------
int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    curs_set(1);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN,  -1); // match
        init_pair(2, COLOR_RED,    -1); // delete
        init_pair(3, COLOR_BLUE,   -1); // insert
        init_pair(4, COLOR_WHITE,  -1); // labels
        init_pair(5, COLOR_CYAN,   -1); // headings
    }

    string S, T;
    int active = 0; // 0 -> S, 1 -> T
    const size_t MAX_LEN = 120;

    const int inputH = 3;
    const int headerH = 1;

    WINDOW* wLeft = nullptr;
    WINDOW* wRight = nullptr;
    WINDOW* wOut = nullptr;

    auto rebuildWindows = [&]() {
        if (wLeft) delwin(wLeft);
        if (wRight) delwin(wRight);
        if (wOut) delwin(wOut);

        int H, W;
        getmaxyx(stdscr, H, W);

        int topH = headerH + inputH;
        int outY = topH;
        int outH = max(3, H - outY);

        int halfW = W / 2;

        wLeft  = newwin(inputH, halfW, headerH, 0);
        wRight = newwin(inputH, W - halfW, headerH, halfW);
        wOut   = newwin(outH, W, outY, 0);
    };

    rebuildWindows();

    bool dirty = true;

    DPBundle bundle;
    bool haveBundle = false;
    HistoryIterator it;
    vector<vector<Op>> cache; // only generated histories so far
    size_t scroll = 0;

    auto recompute = [&]() {
        bundle = computeDPWithTransitions(S, T);
        haveBundle = true;
        it.reset(&bundle, (int)S.size(), (int)T.size());
        cache.clear();
        scroll = 0;
        dirty = true;
    };

    recompute();

    auto ensureCache = [&](size_t needCount) {
        if (!haveBundle) return;
        vector<Op> tmp;
        while (cache.size() < needCount) {
            if (!it.next(tmp)) break;
            cache.push_back(std::move(tmp));
        }
    };

    while (true) {
        int H, W;
        getmaxyx(stdscr, H, W);

        static int lastH = -1, lastW = -1;
        if (H != lastH || W != lastW) {
            lastH = H; lastW = W;

            refresh();
            clear();
            rebuildWindows();
            dirty = true;
        }

        if (dirty) {
            erase();
            attron(COLOR_PAIR(5) | A_BOLD);
            mvprintw(0, 0, "Histories (lazy, minimal) | Tab switch | \u2191/\u2193 or k/j scroll | Backspace delete | Ctrl+U clear | q/Esc quit");
            attroff(COLOR_PAIR(5) | A_BOLD);

            drawBoxWithTitle(wLeft,  "S (source)");
            drawInput(wLeft, S, active == 0);
            drawBoxWithTitle(wRight, "T (target)");
            drawInput(wRight, T, active == 1);

            werase(wOut);
            box(wOut, 0, 0);



            int outH, outW;
            getmaxyx(wOut, outH, outW);

            // how many lines can we show?
            int headerLines = 4; // dist/legend etc.
            int viewLines = max(0, outH - headerLines - 1); // leave border
            size_t viewCount = (size_t)viewLines;

            // ensure cache up to scroll+viewCount
            ensureCache(scroll + viewCount);

            wattron(wOut, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(wOut, 1, 2, "dist=%d | generated=%zu | scroll=%zu | Slen=%zu Tlen=%zu",
                      bundle.dist, cache.size(), scroll, S.size(), T.size());
            wattroff(wOut, COLOR_PAIR(4) | A_BOLD);

            mvwprintw(wOut, 2, 2, "Legend: ");
            wattron(wOut, COLOR_PAIR(1)); wprintw(wOut, "match "); wattroff(wOut, COLOR_PAIR(1));
            wattron(wOut, COLOR_PAIR(2) | A_DIM); wprintw(wOut, "delete "); wattroff(wOut, COLOR_PAIR(2) | A_DIM);
            wattron(wOut, COLOR_PAIR(3) | A_BOLD | A_UNDERLINE); wprintw(wOut, "insert "); wattroff(wOut, COLOR_PAIR(3) | A_BOLD | A_UNDERLINE);

            mvwprintw(wOut, 3, 2, "Showing %zu histories (lazy).", viewCount);

            int y = 4;
            for (size_t k = 0; k < viewCount; k++) {
                size_t idx = scroll + k;
                if (idx >= cache.size()) break;
                if (y >= outH - 1) break;

                wattron(wOut, COLOR_PAIR(5) | A_BOLD);
                mvwprintw(wOut, y, 2, "%06zu:", idx + 1);
                wattroff(wOut, COLOR_PAIR(5) | A_BOLD);

                int maxWidth = max(0, outW - 10);
                printColoredOps(wOut, y, 10, cache[idx], maxWidth);
                y++;
            }

            if (cache.empty()) {
                mvwprintw(wOut, 4, 2, "(no histories?)");
            } else if (scroll + viewCount > cache.size() && it.done()) {
                mvwprintw(wOut, outH - 2, 2, "(end reached)");
            }

            //wrefresh(wOut);
	    commitFrame(wLeft, wRight, wOut);

            dirty = false;
        }

        int ch = getch();
        if (ch == 'q' || ch == 27) break;

        if (ch == '\t') { active = 1 - active; dirty = true; continue; }

        if (ch == 21) { // Ctrl+U
            if (active == 0) S.clear(); else T.clear();
            recompute();
            continue;
        }

        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (active == 0) { if (!S.empty()) S.pop_back(); }
            else { if (!T.empty()) T.pop_back(); }
            recompute();
            continue;
        }

        // Scroll
        if (ch == KEY_DOWN ) {
            scroll++;
            dirty = true;
            continue;
        }
        if (ch == KEY_UP ) {
            if (scroll > 0) scroll--;
            dirty = true;
            continue;
        }
        if (ch == KEY_NPAGE) { // PageDown
            // jump by visible lines
            int outH, outW; getmaxyx(wOut, outH, outW);
            int viewLines = max(1, outH - 4 - 1);
            scroll += (size_t)viewLines;
            dirty = true;
            continue;
        }
        if (ch == KEY_PPAGE) { // PageUp
            int outH, outW; getmaxyx(wOut, outH, outW);
            int viewLines = max(1, outH - 4 - 1);
            scroll = (scroll > (size_t)viewLines) ? (scroll - (size_t)viewLines) : 0;
            dirty = true;
            continue;
        }

        // Input (printable ASCII)
        if (ch >= 32 && ch <= 126) {
            string& cur = (active == 0) ? S : T;
            if (cur.size() < MAX_LEN) {
                cur.push_back((char)ch);
                recompute();
            }
            continue;
        }
    }

    endwin();
    return 0;
}

