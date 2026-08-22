#include "opening_book.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

const char  BOOK_MAGIC[8] = {'C','V','O','P','N','0','1','\0'};
const char *BOOK_NAME     = ".chess_viewer_openings";

#ifdef _WIN32
const char SEP = '\\';
#else
const char SEP = '/';
#endif

void put_u32(FILE *f, unsigned int v) { fwrite(&v, sizeof(v), 1, f); }
void put_u64(FILE *f, unsigned long long v) { fwrite(&v, sizeof(v), 1, f); }
bool get_u32(FILE *f, unsigned int &v) { return fread(&v, sizeof(v), 1, f) == 1; }
bool get_u64(FILE *f, unsigned long long &v) { return fread(&v, sizeof(v), 1, f) == 1; }

} // namespace

std::string OpeningBook::book_path(const std::string &games_dir) {
    if (games_dir.empty()) return BOOK_NAME;
    char last = games_dir[games_dir.size() - 1];
    if (last == '/' || last == '\\') return games_dir + BOOK_NAME;
    return games_dir + SEP + BOOK_NAME;
}

void OpeningBook::clear() {
    nodes_.clear();
    order_.clear();
    kids_.clear();
    build_.clear();
    game_ids_.clear();
    deepest_.clear();
    ready_ = false;
}

const OpeningNode &OpeningBook::node(int i) const {
    static const OpeningNode empty;
    if (i < 0 || i >= (int)nodes_.size()) return empty;
    return nodes_[(size_t)i];
}

int OpeningBook::child_with_key(int parent, unsigned long long key) const {
    for (int c : children(parent))
        if (nodes_[(size_t)c].key == key) return c;
    return -1;
}

std::vector<int> OpeningBook::children(int parent) const {
    std::vector<int> out;
    if (parent < 0 || parent >= (int)nodes_.size()) return out;
    const OpeningNode &p = nodes_[(size_t)parent];
    for (int i = 0; i < p.child_count; i++) {
        int slot = p.first_child + i;
        if (slot >= 0 && slot < (int)kids_.size()) out.push_back(kids_[(size_t)slot]);
    }
    return out;
}

std::vector<int> OpeningBook::games(int n) const {
    std::vector<int> out;
    if (n < 0 || n >= (int)nodes_.size()) return out;
    const OpeningNode &node = nodes_[(size_t)n];
    for (int i = 0; i < node.game_count; i++) {
        int k = node.first_game + i;
        if (k >= 0 && k < (int)order_.size()) out.push_back(order_[(size_t)k]);
    }
    return out;
}

// ── Building ────────────────────────────────────────────────────────────────

void OpeningBook::begin_build() {
    clear();
    BuildNode root;
    root.depth = 0;
    build_.push_back(root);
}

void OpeningBook::add_game(int game_id, const unsigned long long *keys,
                           const char (*moves)[8], int n) {
    if (build_.empty()) begin_build();
    if (n > MAX_PLIES) n = MAX_PLIES;

    int cur = 0;
    build_[0].hits++;
    for (int d = 0; d < n; d++) {
        int found = -1;
        for (size_t i = 0; i < build_[(size_t)cur].kids.size(); i++) {
            int k = build_[(size_t)cur].kids[i];
            if (build_[(size_t)k].key == keys[d]) { found = k; break; }
        }
        if (found < 0) {
            BuildNode nn;
            nn.key    = keys[d];
            nn.parent = cur;
            nn.depth  = (short)(d + 1);
            snprintf(nn.move, sizeof(nn.move), "%s", moves[d]);
            build_.push_back(nn);
            found = (int)build_.size() - 1;
            build_[(size_t)cur].kids.push_back(found);
        }
        build_[(size_t)found].hits++;
        cur = found;
    }
    game_ids_.push_back(game_id);
    deepest_.push_back(cur);
}

void OpeningBook::finish_build() {
    nodes_.clear();
    order_.clear();
    kids_.clear();
    if (build_.empty()) { ready_ = true; return; }

    // Most-played first, so the catalog lists real continuations before oddities.
    for (size_t i = 0; i < build_.size(); i++) {
        std::vector<int> &kids = build_[i].kids;
        std::sort(kids.begin(), kids.end(), [this](int a, int b) {
            if (build_[(size_t)a].hits != build_[(size_t)b].hits)
                return build_[(size_t)a].hits > build_[(size_t)b].hits;
            return strcmp(build_[(size_t)a].move, build_[(size_t)b].move) < 0;
        });
    }

    // Flatten depth-first, so a node's subtree is a contiguous index range and
    // its games become a contiguous range too.
    std::vector<int> dfs_of(build_.size(), -1);   // build index -> node index
    std::vector<int> subtree(build_.size(), 0);
    nodes_.reserve(build_.size());
    {
        // Explicit stack: the tree is a million nodes deep in the worst case and
        // recursion here would be at the mercy of the stack limit.
        struct Frame { int b; int child; int self; };
        std::vector<Frame> st;
        st.push_back({0, 0, -1});
        while (!st.empty()) {
            Frame &fr = st.back();
            if (fr.self < 0) {
                fr.self = (int)nodes_.size();
                dfs_of[(size_t)fr.b] = fr.self;
                OpeningNode nn;
                nn.key   = build_[(size_t)fr.b].key;
                nn.depth = build_[(size_t)fr.b].depth;
                memcpy(nn.move, build_[(size_t)fr.b].move, sizeof(nn.move));
                nn.child_count = (short)build_[(size_t)fr.b].kids.size();
                nn.first_child = -1;
                nodes_.push_back(nn);
            }
            if (fr.child < (int)build_[(size_t)fr.b].kids.size()) {
                int kid = build_[(size_t)fr.b].kids[(size_t)fr.child];
                fr.child++;
                st.push_back({kid, 0, -1});
            } else {
                subtree[(size_t)fr.b] = (int)nodes_.size() - fr.self;
                st.pop_back();
            }
        }
    }

    // Children go in their own table, since DFS order scatters them. Filled in a
    // second pass now that every build node has a node index.
    kids_.reserve(build_.size());
    for (size_t b = 0; b < build_.size(); b++) {
        int n = dfs_of[b];
        if (n < 0) continue;
        const std::vector<int> &bk = build_[b].kids;
        nodes_[(size_t)n].first_child = bk.empty() ? -1 : (int)kids_.size();
        nodes_[(size_t)n].child_count = (short)bk.size();
        for (size_t i = 0; i < bk.size(); i++) kids_.push_back(dfs_of[(size_t)bk[i]]);
    }

    // Sort the games by where their deepest node landed in that order; every
    // node's games are then the slice whose deepest node falls in its subtree.
    std::vector<int> idx(game_ids_.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return dfs_of[(size_t)deepest_[(size_t)a]] < dfs_of[(size_t)deepest_[(size_t)b]];
    });
    order_.resize(idx.size());
    std::vector<int> sorted_pos(idx.size());
    for (size_t i = 0; i < idx.size(); i++) {
        order_[i]      = game_ids_[(size_t)idx[i]];
        sorted_pos[i]  = dfs_of[(size_t)deepest_[(size_t)idx[i]]];
    }
    for (size_t b = 0; b < build_.size(); b++) {
        int n = dfs_of[b];
        if (n < 0) continue;
        int lo = (int)(std::lower_bound(sorted_pos.begin(), sorted_pos.end(), n) - sorted_pos.begin());
        int hi = (int)(std::lower_bound(sorted_pos.begin(), sorted_pos.end(), n + subtree[b]) - sorted_pos.begin());
        nodes_[(size_t)n].first_game = lo;
        nodes_[(size_t)n].game_count = hi - lo;
    }

    build_.clear();
    build_.shrink_to_fit();
    game_ids_.clear();
    game_ids_.shrink_to_fit();
    deepest_.clear();
    deepest_.shrink_to_fit();
    ready_ = true;
}

// ── Cache ───────────────────────────────────────────────────────────────────

bool OpeningBook::save(const std::string &path, unsigned long long fingerprint) const {
    if (!ready_) return false;
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(BOOK_MAGIC, 1, sizeof(BOOK_MAGIC), f);
    put_u64(f, fingerprint);
    put_u32(f, (unsigned int)MAX_PLIES);

    put_u32(f, (unsigned int)nodes_.size());
    for (const OpeningNode &n : nodes_) {
        // Field by field, so the file does not depend on this compiler's padding.
        put_u64(f, n.key);
        put_u32(f, (unsigned int)n.first_game);
        put_u32(f, (unsigned int)n.game_count);
        put_u32(f, (unsigned int)n.first_child);
        put_u32(f, (unsigned int)(unsigned short)n.child_count);
        put_u32(f, (unsigned int)(unsigned short)n.depth);
        fwrite(n.move, 1, sizeof(n.move), f);
    }
    put_u32(f, (unsigned int)kids_.size());
    if (!kids_.empty()) fwrite(kids_.data(), sizeof(int), kids_.size(), f);
    put_u32(f, (unsigned int)order_.size());
    if (!order_.empty()) fwrite(order_.data(), sizeof(int), order_.size(), f);
    fclose(f);
    return true;
}

bool OpeningBook::load(const std::string &path, unsigned long long fingerprint) {
    clear();
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[sizeof(BOOK_MAGIC)];
    unsigned long long fp = 0;
    unsigned int plies = 0, n = 0;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, BOOK_MAGIC, sizeof(magic)) != 0 ||
        !get_u64(f, fp) || fp != fingerprint ||
        !get_u32(f, plies) || (int)plies != MAX_PLIES ||
        !get_u32(f, n) || n > 20000000u) {
        fclose(f);
        return false;
    }
    nodes_.resize(n);
    for (unsigned int i = 0; i < n; i++) {
        unsigned int fg = 0, gc = 0, fc = 0, cc = 0, dp = 0;
        OpeningNode &nd = nodes_[i];
        if (!get_u64(f, nd.key) || !get_u32(f, fg) || !get_u32(f, gc) ||
            !get_u32(f, fc) || !get_u32(f, cc) || !get_u32(f, dp) ||
            fread(nd.move, 1, sizeof(nd.move), f) != sizeof(nd.move)) {
            fclose(f); clear(); return false;
        }
        nd.first_game  = (int)fg;
        nd.game_count  = (int)gc;
        nd.first_child = (int)fc;
        nd.child_count = (short)(unsigned short)cc;
        nd.depth       = (short)(unsigned short)dp;
        nd.move[sizeof(nd.move) - 1] = 0;
    }
    unsigned int k = 0;
    if (!get_u32(f, k) || k > 50000000u) { fclose(f); clear(); return false; }
    kids_.resize(k);
    if (k && fread(kids_.data(), sizeof(int), k, f) != k) {
        fclose(f); clear(); return false;
    }
    unsigned int g = 0;
    if (!get_u32(f, g) || g > 50000000u) { fclose(f); clear(); return false; }
    order_.resize(g);
    if (g && fread(order_.data(), sizeof(int), g, f) != g) {
        fclose(f); clear(); return false;
    }
    fclose(f);
    ready_ = true;
    return true;
}
