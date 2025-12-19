#include <bits/stdc++.h>
using namespace std;

static const int INF = 1e9;

// Bits for prev moves
// We store backpointers for optimal transitions INTO (i,j).
// M: came from (i-1, j-1) with cost 0 if S[i-1]==T[j-1]
// D: came from (i-1, j)   with cost 1 (delete S[i-1])
// I: came from (i,   j-1) with cost 1 (insert T[j-1])
enum PrevBits : uint8_t {
    PREV_NONE = 0,
    PREV_M    = 1 << 0,
    PREV_D    = 1 << 1,
    PREV_I    = 1 << 2
};

enum OpType { OP_M, OP_D, OP_I };

struct Op {
    OpType t;
    char ch; // for M: matched char, for D: deleted char from S, for I: inserted char from T
};

// Build pretty alignment from an op-sequence (forward order).
static pair<string,string> buildAlignment(const vector<Op>& ops) {
    string a, b;
    a.reserve(ops.size());
    b.reserve(ops.size());
    for (const auto& op : ops) {
        switch (op.t) {
            case OP_M:
                a.push_back(op.ch);
                b.push_back(op.ch);
                break;
            case OP_D:
                a.push_back(op.ch);
                b.push_back('_');
                break;
            case OP_I:
                a.push_back('_');
                b.push_back(op.ch);
                break;
        }
    }
    return {a, b};
}

// Deterministic ordering of branches at a node.
// Order: M first (0-cost), then D, then I.
// If same op-type, sort by char (to get "alphabet-like" stability).
static int opRank(OpType t) {
    switch (t) {
        case OP_M: return 0;
        case OP_D: return 1;
        case OP_I: return 2;
    }
    return 3;
}

struct Transition {
    int pi, pj;     // parent cell
    Op op;          // operation to add (in reverse path it's still op at this step)
};

static vector<Transition> getTransitions(
    int i, int j,
    const string& S, const string& T,
    const vector<vector<uint8_t>>& prev
) {
    vector<Transition> tr;
    uint8_t mask = prev[i][j];

    if (mask & PREV_M) {
        // parent (i-1, j-1), matched char is S[i-1] == T[j-1]
        tr.push_back({i-1, j-1, {OP_M, S[i-1]}});
    }
    if (mask & PREV_D) {
        // parent (i-1, j), deleted char is S[i-1]
        tr.push_back({i-1, j, {OP_D, S[i-1]}});
    }
    if (mask & PREV_I) {
        // parent (i, j-1), inserted char is T[j-1]
        tr.push_back({i, j-1, {OP_I, T[j-1]}});
    }

    sort(tr.begin(), tr.end(), [](const Transition& a, const Transition& b){
        int ra = opRank(a.op.t);
        int rb = opRank(b.op.t);
        if (ra != rb) return ra < rb;
        if (a.op.ch != b.op.ch) return a.op.ch < b.op.ch;
        // tiebreaker for full determinism
        if (a.pi != b.pi) return a.pi < b.pi;
        return a.pj < b.pj;
    });

    return tr;
}

// Enumerate histories (minimal scripts) by DFS over backpointer DAG.
// We traverse from (n,m) -> (0,0) using prev[][], collecting ops in reverse,
// then reverse them for output.
static void dfsEnumerate(
    int i, int j,
    const string& S, const string& T,
    const vector<vector<uint8_t>>& prev,
    vector<Op>& opsRev,
    size_t& produced,
    size_t limit
) {
    if (produced >= limit) return;

    if (i == 0 && j == 0) {
        // We have one full history in reverse; print forward.
        vector<Op> ops = opsRev;
        reverse(ops.begin(), ops.end());

        auto [aLine, bLine] = buildAlignment(ops);

        cout << "\n=== History #" << (produced + 1) << " ===\n";
        cout << aLine << "\n" << bLine << "\n";

        // Also print ops as a compact script
        cout << "Ops: ";
        for (const auto& op : ops) {
            if (op.t == OP_M) cout << "M(" << op.ch << ") ";
            else if (op.t == OP_D) cout << "D(" << op.ch << ") ";
            else cout << "I(" << op.ch << ") ";
        }
        cout << "\n";

        produced++;
        return;
    }

    // Expand possible optimal parents of (i,j)
    for (const auto& tr : getTransitions(i, j, S, T, prev)) {
        if (produced >= limit) return;
        opsRev.push_back(tr.op);
        dfsEnumerate(tr.pi, tr.pj, S, T, prev, opsRev, produced, limit);
        opsRev.pop_back();
    }
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " S T [max_histories]\n";
        return 1;
    }

    string S = argv[1];
    string T = argv[2];
    size_t maxHist = 20;
    if (argc >= 4) {
        maxHist = (size_t)stoull(argv[3]);
        if (maxHist == 0) maxHist = 1;
    }

    const int n = (int)S.size();
    const int m = (int)T.size();

    // dp and prev are (n+1) x (m+1)
    vector<vector<int>> dp(n+1, vector<int>(m+1, INF));
    vector<vector<uint8_t>> prev(n+1, vector<uint8_t>(m+1, PREV_NONE));

    dp[0][0] = 0;

    // Base cases: transform S[:i] -> "" is i deletes
    for (int i = 1; i <= n; i++) {
        dp[i][0] = i;
        prev[i][0] = PREV_D; // from (i-1,0)
    }
    // Base cases: transform "" -> T[:j] is j inserts
    for (int j = 1; j <= m; j++) {
        dp[0][j] = j;
        prev[0][j] = PREV_I; // from (0,j-1)
    }

    // Fill DP
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            int best = INF;
            uint8_t mask = PREV_NONE;

            // Delete
            {
                int v = dp[i-1][j] + 1;
                if (v < best) { best = v; mask = PREV_D; }
                else if (v == best) mask |= PREV_D;
            }
            // Insert
            {
                int v = dp[i][j-1] + 1;
                if (v < best) { best = v; mask = PREV_I; }
                else if (v == best) mask |= PREV_I;
            }
            // Match/Keep (cost 0)
            if (S[i-1] == T[j-1]) {
                int v = dp[i-1][j-1];
                if (v < best) { best = v; mask = PREV_M; }
                else if (v == best) mask |= PREV_M;
            }

            dp[i][j] = best;
            prev[i][j] = mask;
        }
    }

    cout << "S: " << S << "\n";
    cout << "T: " << T << "\n";
    cout << "Minimal insert/delete distance = " << dp[n][m] << "\n";
    cout << "Enumerating up to " << maxHist << " minimal histories...\n";

    size_t produced = 0;
    vector<Op> opsRev;
    opsRev.reserve((size_t)n + (size_t)m);

    dfsEnumerate(n, m, S, T, prev, opsRev, produced, maxHist);

    cout << "\nProduced " << produced << " histories.\n";
    if (produced == maxHist) {
        cout << "(limit reached; there may be more minimal histories)\n";
    }

    return 0;
}

