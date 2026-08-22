// Tests for the opening book.
//
// The book takes positions already hashed, so these use synthetic keys rather
// than real chess — which is the point of the split: the tree, the game ranges
// and the cache can all be checked without a board anywhere in sight.
#include "opening_book.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what) {
    checks++;
    if (!cond) { failures++; std::printf("  FAIL: %s\n", what); }
}
static void check_eq(int got, int want, const char *what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("  FAIL: %s (want %d, got %d)\n", what, want, got);
    }
}

// A line is a list of move names; each move hashes to a stable key so the same
// prefix in two games lands on the same node.
static unsigned long long key_of(const std::string &prefix) {
    unsigned long long h = 1469598103934665603ULL;
    for (char c : prefix) { h ^= (unsigned char)c; h *= 1099511628211ULL; }
    return h;
}

static void add_line(OpeningBook &b, int game_id, const std::vector<std::string> &moves) {
    unsigned long long keys[OpeningBook::MAX_PLIES];
    char mv[OpeningBook::MAX_PLIES][8];
    std::string prefix;
    int n = (int)moves.size();
    if (n > OpeningBook::MAX_PLIES) n = OpeningBook::MAX_PLIES;
    for (int i = 0; i < n; i++) {
        prefix += moves[(size_t)i];
        prefix += "|";
        keys[i] = key_of(prefix);
        std::snprintf(mv[i], sizeof(mv[i]), "%s", moves[(size_t)i].c_str());
    }
    b.add_game(game_id, keys, mv, n);
}

// Walk from the root along `moves`, or -1 if the line is not in the book.
static int walk(const OpeningBook &b, const std::vector<std::string> &moves) {
    int node = b.root();
    std::string prefix;
    for (const std::string &m : moves) {
        prefix += m;
        prefix += "|";
        node = b.child_with_key(node, key_of(prefix));
        if (node < 0) return -1;
    }
    return node;
}

static const char *SCRATCH = "opening_test_scratch.bin";

// ── Tests ────────────────────────────────────────────────────────────────────

static void test_shape() {
    std::printf("[tree shape]\n");
    OpeningBook b;
    b.begin_build();
    add_line(b, 10, {"e4", "e5", "f4"});          // King's Gambit
    add_line(b, 11, {"e4", "e5", "f4"});
    add_line(b, 12, {"e4", "e5", "Nf3"});         // shares "e4 e5"
    add_line(b, 13, {"e4", "c5"});                // shares "e4"
    add_line(b, 14, {"d4"});
    b.finish_build();

    check(b.ready(), "book reports ready");
    check_eq(b.game_count(), 5, "every game accounted for");
    check_eq(b.node(b.root()).game_count, 5, "root holds all games");

    // Prefixes merge rather than duplicating.
    check_eq((int)b.children(b.root()).size(), 2, "two distinct first moves");

    int e4 = walk(b, {"e4"});
    check(e4 > 0, "e4 node exists");
    check_eq(b.node(e4).game_count, 4, "four games start with e4");
    check_eq((int)b.children(e4).size(), 2, "e4 has two replies");

    int e5 = walk(b, {"e4", "e5"});
    check_eq(b.node(e5).game_count, 3, "three games reach e4 e5");
    check_eq(b.node(e5).depth, 2, "depth counted in plies");

    int kg = walk(b, {"e4", "e5", "f4"});
    check_eq(b.node(kg).game_count, 2, "two games reach the gambit");
    check_eq((int)b.children(kg).size(), 0, "nothing recorded past it");
    check(strcmp(b.node(kg).move, "f4") == 0, "node remembers the move that made it");

    check_eq(walk(b, {"e4", "e5", "Bc4"}), -1, "unplayed line is absent");
    check_eq(walk(b, {"h4"}), -1, "unplayed first move is absent");
}

static void test_games_are_exact() {
    std::printf("[game ranges]\n");
    OpeningBook b;
    b.begin_build();
    add_line(b, 100, {"e4", "e5", "f4"});
    add_line(b, 101, {"e4", "e5", "Nf3"});
    add_line(b, 102, {"e4", "c5"});
    add_line(b, 103, {"d4", "d5"});
    b.finish_build();

    auto has = [](const std::vector<int> &v, int x) {
        for (int i : v) if (i == x) return true;
        return false;
    };

    std::vector<int> all = b.games(b.root());
    check_eq((int)all.size(), 4, "root returns every game");

    std::vector<int> e4 = b.games(walk(b, {"e4"}));
    check_eq((int)e4.size(), 3, "e4 returns three");
    check(has(e4, 100) && has(e4, 101) && has(e4, 102), "e4 returns the right three");
    check(!has(e4, 103), "the d4 game is not among them");

    std::vector<int> kg = b.games(walk(b, {"e4", "e5", "f4"}));
    check_eq((int)kg.size(), 1, "the gambit returns one");
    check(has(kg, 100), "and it is the right one");

    std::vector<int> d4 = b.games(walk(b, {"d4"}));
    check_eq((int)d4.size(), 1, "d4 returns one");
    check(has(d4, 103), "and it is the right one");
}

static void test_children_ordered_by_popularity() {
    std::printf("[continuation order]\n");
    OpeningBook b;
    b.begin_build();
    for (int i = 0; i < 5; i++) add_line(b, i,      {"e4", "c5"});    // 5
    for (int i = 0; i < 9; i++) add_line(b, 100 + i, {"e4", "e5"});   // 9
    for (int i = 0; i < 2; i++) add_line(b, 200 + i, {"e4", "e6"});   // 2
    b.finish_build();

    std::vector<int> kids = b.children(walk(b, {"e4"}));
    check_eq((int)kids.size(), 3, "three replies recorded");
    check(strcmp(b.node(kids[0]).move, "e5") == 0, "most played first");
    check(strcmp(b.node(kids[1]).move, "c5") == 0, "then the next");
    check(strcmp(b.node(kids[2]).move, "e6") == 0, "least played last");
    check_eq(b.node(kids[0]).game_count, 9, "counts match the traffic");
    check_eq(b.node(kids[2]).game_count, 2, "counts match the traffic");
}

static void test_depth_cap() {
    std::printf("[depth cap]\n");
    OpeningBook b;
    b.begin_build();
    std::vector<std::string> deep;
    for (int i = 0; i < OpeningBook::MAX_PLIES + 6; i++) deep.push_back("m" + std::to_string(i));
    add_line(b, 1, deep);
    b.finish_build();

    int node = b.root();
    int depth = 0;
    while (!b.children(node).empty()) { node = b.children(node)[0]; depth++; }
    check_eq(depth, OpeningBook::MAX_PLIES, "tree stops at the cap");
    check_eq(b.node(node).depth, OpeningBook::MAX_PLIES, "deepest node reports the cap");
}

static void test_cache() {
    std::printf("[cache]\n");
    std::remove(SCRATCH);
    OpeningBook a;
    a.begin_build();
    add_line(a, 7, {"e4", "e5", "f4"});
    add_line(a, 8, {"e4", "c5"});
    a.finish_build();
    check(a.save(SCRATCH, 0xABCDEF01ULL), "saves");

    OpeningBook b;
    check(b.load(SCRATCH, 0xABCDEF01ULL), "loads");
    check_eq(b.node_count(), a.node_count(), "same node count");
    check_eq(b.game_count(), a.game_count(), "same game count");
    int kg = walk(b, {"e4", "e5", "f4"});
    check(kg > 0, "line still reachable after reload");
    check_eq(b.node(kg).game_count, 1, "counts survive");
    check_eq((int)b.games(kg).size(), 1, "games survive");
    check_eq(b.games(kg)[0], 7, "and are the right ids");
    check(strcmp(b.node(kg).move, "f4") == 0, "move text survives");
    check_eq((int)b.children(walk(b, {"e4"})).size(), 2, "child table survives");

    // A collection that changed must not be answered from the old book.
    OpeningBook c;
    check(!c.load(SCRATCH, 0x99999999ULL), "different fingerprint is rejected");
    check(!c.ready(), "and leaves the book unusable");
    std::remove(SCRATCH);
}

static void test_corrupt_cache() {
    std::printf("[corrupt cache]\n");
    const char *junk[] = { "", "CVOPN01", "not a book at all", "CVOPN01\xff\xff\xff\xff" };
    const size_t lens[] = { 0, 7, 17, 12 };
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(SCRATCH, "wb");
        if (f) { if (lens[i]) fwrite(junk[i], 1, lens[i], f); fclose(f); }
        OpeningBook b;
        char msg[80];
        std::snprintf(msg, sizeof(msg), "corrupt cache %d rejected cleanly", i);
        check(!b.load(SCRATCH, 1ULL), msg);
        check(!b.ready(), "book left unusable");
    }
    std::remove(SCRATCH);
}

static void test_empty() {
    std::printf("[empty]\n");
    OpeningBook b;
    b.begin_build();
    b.finish_build();
    check(b.ready(), "empty build still finishes");
    check_eq(b.game_count(), 0, "no games");
    check_eq((int)b.children(b.root()).size(), 0, "root has no children");
    check_eq((int)b.games(b.root()).size(), 0, "root has no games");
    check_eq(b.child_with_key(b.root(), 123), -1, "lookup on empty is safe");
    check_eq((int)b.games(-1).size(), 0, "invalid node is safe");
    check_eq((int)b.children(9999).size(), 0, "out-of-range node is safe");
}

int main() {
    std::printf("opening book self-test\n\n");
    test_shape();
    test_games_are_exact();
    test_children_ordered_by_popularity();
    test_depth_cap();
    test_cache();
    test_corrupt_cache();
    test_empty();
    std::remove(SCRATCH);
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
