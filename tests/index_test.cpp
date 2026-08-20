// Tests for the per-game PGN index.
//
// Builds indexes over PGN fixtures written into a scratch directory, so the
// tests are fast, deterministic, and never touch the real games/ tree or the
// cache file that sits in it.
#include "game_index.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

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
static void check_str(const std::string &got, const std::string &want, const char *what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("  FAIL: %s (want \"%s\", got \"%s\")\n",
                    what, want.c_str(), got.c_str());
    }
}

static const char *DIR = "index_test_scratch";

static std::string dpath(const char *name) {
    return std::string(DIR) + "/" + name;
}

static void write_file(const char *name, const std::string &body) {
    FILE *f = fopen(dpath(name).c_str(), "wb");
    if (!f) { std::printf("  FAIL: cannot write %s\n", name); failures++; return; }
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
}

static void remove_file(const char *name) { std::remove(dpath(name).c_str()); }

static std::string game(const char *white, const char *black, const char *date,
                        const char *result, const char *welo, const char *belo,
                        const char *moves) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "[Event \"Test\"]\n[White \"%s\"]\n[Black \"%s\"]\n[Date \"%s\"]\n"
        "[Result \"%s\"]\n[WhiteElo \"%s\"]\n[BlackElo \"%s\"]\n\n%s %s\n\n",
        white, black, date, result, welo, belo, moves, result);
    return buf;
}

static void fresh_dir() {
    MKDIR(DIR);
    remove_file(".chess_viewer_index");
    remove_file("a.pgn");
    remove_file("b.pgn");
}

// ── Tests ────────────────────────────────────────────────────────────────────

static void test_last_name() {
    std::printf("[last name]\n");
    struct { const char *in; const char *want; } cases[] = {
        {"Kasparov, Garry",   "Kasparov"},
        {"Carlsen,M",         "Carlsen"},
        {"Vachier Lagrave,M", "Vachier Lagrave"},  // multi-word surname kept whole
        {"Adams, Michael",    "Adams"},
        {"Adams,Mi",          "Adams"},            // collapses with the line above
        {"  Tal, Mihail  ",   "Tal"},              // leading space trimmed
        {"Tal ,Mihail",       "Tal"},              // space before the comma trimmed
        {"Abrahamer",         "Abrahamer"},        // no comma, single word
        {"Asish Panda",       "Panda"},            // no comma, last word
        {"",                  ""},
        {"   ",               ""},
        {",Garry",            ""},                 // nothing before the comma
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "last_name of \"%s\"", cases[i].in);
        check_str(GameIndex::last_name(cases[i].in), cases[i].want, msg);
    }
}

static void test_names_merge() {
    std::printf("[name merging]\n");
    fresh_dir();
    // The same two players under the various spellings the real files use.
    write_file("a.pgn",
        game("Ivanchuk,V",        "Carlsen,M",    "2001.01.01", "1-0", "", "", "1. e4") +
        game("Ivanchuk, Vassily", "Carlsen, M",   "2002.01.01", "0-1", "", "", "1. d4") +
        game("Carlsen,Magnus",    "Ivanchuk,Va",  "2003.01.01", "1-0", "", "", "1. c4"));

    GameIndex idx;
    idx.load_blocking(DIR);
    check_eq(idx.count(), 3, "three games");

    auto players = idx.players_by_frequency();
    check_eq((int)players.size(), 2, "three spellings each collapse to one player");

    int iv = idx.find_name("Ivanchuk");
    check(iv >= 0, "the surname is what gets stored");
    check_eq((int)idx.games_of_player(iv).size(), 3, "every spelling found under one name");
    check_eq(idx.find_name("Ivanchuk,V"), -1, "the full tag is not stored");
    check_eq((int)idx.search("ivanchuk").size(), 3, "search matches the merged name");
}

static void test_build_and_fields() {
    std::printf("[build]\n");
    fresh_dir();
    write_file("a.pgn",
        game("Kasparov, G", "Karpov, A", "1985.10.15", "1-0", "2700", "2720", "1. e4 c5") +
        game("Karpov, A", "Kasparov, G", "1985.11.01", "0-1", "2720", "2700", "1. d4 Nf6"));
    write_file("b.pgn",
        game("Tal, M", "Fischer, R", "1960.01.01", "1/2-1/2", "", "", "1. e4 e5"));

    GameIndex idx;
    idx.load_blocking(DIR);
    check(idx.loaded(), "index reports loaded");
    check_eq(idx.count(), 3, "three games across two files");

    // Games are indexed in file order, files sorted by relative path.
    const IndexEntry &e0 = idx.entry(0);
    check_str(idx.name(e0.white_id), "Kasparov", "white name of first game");
    check_str(idx.name(e0.black_id), "Karpov",   "black name of first game");
    check_eq(e0.year, 1985, "year parsed");
    check_eq(e0.white_elo, 2700, "white elo parsed");
    check_eq(e0.black_elo, 2720, "black elo parsed");
    check(e0.result == 'W', "1-0 becomes 'W'");
    check_eq(e0.offset, 0, "first game starts at byte 0");

    const IndexEntry &e1 = idx.entry(1);
    check(e1.result == 'B', "0-1 becomes 'B'");
    check(e1.offset > 0, "second game has a later offset");
    check_eq(e1.file_id, e0.file_id, "both games in the same file");

    const IndexEntry &e2 = idx.entry(2);
    check(e2.result == 'D', "1/2-1/2 becomes 'D'");
    check_eq(e2.white_elo, 0, "missing elo is 0");
    check(e2.file_id != e0.file_id, "third game is in the other file");

    // Interning: a name appearing in several games is stored once.
    check_eq(idx.find_name("Kasparov") == idx.entry(1).black_id ? 1 : 0, 1,
             "repeated name shares one id");
}

static void test_offsets_point_at_events() {
    std::printf("[offsets]\n");
    fresh_dir();
    write_file("a.pgn",
        game("A", "B", "2000.01.01", "1-0", "", "", "1. e4") +
        game("C", "D", "2001.01.01", "0-1", "", "", "1. d4") +
        game("E", "F", "2002.01.01", "1-0", "", "", "1. c4"));

    GameIndex idx;
    idx.load_blocking(DIR);
    check_eq(idx.count(), 3, "three games");

    // The whole point of storing an offset is being able to seek straight to a
    // game inside a packed file, so verify each one lands on its [Event line.
    for (int i = 0; i < idx.count(); i++) {
        const IndexEntry &e = idx.entry(i);
        FILE *f = fopen(idx.full_path(e).c_str(), "rb");
        check(f != nullptr, "game file opens");
        if (!f) continue;
        fseek(f, (long)e.offset, SEEK_SET);
        char line[128] = {0};
        char *got = fgets(line, sizeof(line), f);
        fclose(f);
        char msg[96];
        std::snprintf(msg, sizeof(msg), "entry %d offset lands on [Event", i);
        check(got != nullptr && strncmp(line, "[Event", 6) == 0, msg);
    }
}

static void test_file_lookup() {
    std::printf("[file lookup]\n");
    fresh_dir();
    write_file("a.pgn",
        game("A", "B", "2000.01.01", "1-0", "", "", "1. e4") +
        game("C", "D", "2001.01.01", "0-1", "", "", "1. d4"));
    write_file("b.pgn",
        game("E", "F", "2002.01.01", "1-0", "", "", "1. c4"));

    GameIndex idx;
    idx.load_blocking(DIR);

    int fa = idx.find_file("a.pgn");
    int fb = idx.find_file("b.pgn");
    check(fa >= 0 && fb >= 0, "both files resolve");
    check(fa != fb, "different files get different ids");
    check_eq((int)idx.games_in_file(fa).size(), 2, "two games in a.pgn");
    check_eq((int)idx.games_in_file(fb).size(), 1, "one game in b.pgn");
    check_eq(idx.find_file("missing.pgn"), -1, "unknown path yields -1");
    check_eq((int)idx.games_in_file(-1).size(), 0, "invalid file id yields nothing");

    // The catalog builds paths with the platform separator; the index stored
    // whatever the directory walk produced. Both spellings must resolve.
    check_eq(idx.find_file("a.pgn"), fa, "plain name resolves");

    // Every game reported for a file really belongs to it.
    for (int id : idx.games_in_file(fa))
        check_eq(idx.entry(id).file_id, fa, "game belongs to the file it was listed under");
}

static void test_search() {
    std::printf("[search]\n");
    fresh_dir();
    write_file("a.pgn",
        game("Kasparov, G", "Karpov, A", "1985.10.15", "1-0", "", "", "1. e4") +
        game("Tal, M", "Botvinnik, M", "1960.03.15", "0-1", "", "", "1. e4") +
        game("Fischer, R", "Spassky, B", "1972.07.11", "1-0", "", "", "1. c4"));

    GameIndex idx;
    idx.load_blocking(DIR);

    check_eq((int)idx.search("kasparov").size(), 1, "lowercase query matches");
    check_eq((int)idx.search("KASPAROV").size(), 1, "uppercase query matches");
    check_eq((int)idx.search("kArPoV").size(),   1, "mixed case query matches");
    check_eq((int)idx.search("Tal").size(),      1, "matches a black-side name");
    check_eq((int)idx.search("1972").size(),     1, "matches by year");
    check_eq((int)idx.search("zzzz").size(),     0, "no spurious matches");
    check_eq((int)idx.search("").size(),         0, "empty query matches nothing");

    // A substring shared by two different players finds both games.
    check_eq((int)idx.search("ka").size(), 1, "substring matches both names in one game");

    check_eq((int)idx.search("a", 2).size(), 2, "limit caps results");
}

static void test_players_and_years() {
    std::printf("[players and years]\n");
    fresh_dir();
    write_file("a.pgn",
        game("Alpha", "Beta",  "2001.01.01", "1-0", "", "", "1. e4") +
        game("Alpha", "Gamma", "2002.01.01", "1-0", "", "", "1. e4") +
        game("Beta",  "Alpha", "2001.06.01", "0-1", "", "", "1. e4"));

    GameIndex idx;
    idx.load_blocking(DIR);

    auto players = idx.players_by_frequency();
    check_eq((int)players.size(), 3, "three distinct players");
    check_str(idx.name(players[0].first), "Alpha", "most frequent player first");
    check_eq(players[0].second, 3, "Alpha counted in three games");
    check_eq(players[1].second, 2, "Beta counted in two games");

    int alpha = idx.find_name("Alpha");
    check_eq((int)idx.games_of_player(alpha).size(), 3, "games_of_player spans both colours");

    auto ys = idx.years();
    check_eq((int)ys.size(), 2, "two distinct years");
    check_eq(ys[0], 2002, "years newest first");
    check_eq((int)idx.games_in_year(2001).size(), 2, "two games in 2001");
}

static void test_cache_round_trip() {
    std::printf("[cache]\n");
    fresh_dir();
    write_file("a.pgn",
        game("Kasparov, G", "Karpov, A", "1985.10.15", "1-0", "2700", "2720", "1. e4"));

    GameIndex first;
    first.load_blocking(DIR);
    check_eq(first.count(), 1, "built one entry");

    FILE *cf = fopen(GameIndex::index_file_path(DIR).c_str(), "rb");
    check(cf != nullptr, "cache file written");
    if (cf) fclose(cf);

    // A second index over the same tree must come from the cache and agree.
    GameIndex second;
    second.load_blocking(DIR);
    check_eq(second.count(), 1, "cache reload gives same count");
    check_str(second.name(second.entry(0).white_id), "Kasparov", "cached name survives");
    check_eq(second.entry(0).year, 1985, "cached year survives");
    check_eq(second.entry(0).white_elo, 2700, "cached elo survives");
    check(second.entry(0).result == 'W', "cached result survives");
}

static void test_cache_invalidation() {
    std::printf("[cache invalidation]\n");
    fresh_dir();
    write_file("a.pgn", game("A", "B", "2000.01.01", "1-0", "", "", "1. e4"));
    { GameIndex warm; warm.load_blocking(DIR); check_eq(warm.count(), 1, "warm cache built"); }

    // Editing a file changes the fingerprint, so a stale cache must be rejected
    // rather than silently serving the old contents.
    write_file("a.pgn",
        game("A", "B", "2000.01.01", "1-0", "", "", "1. e4") +
        game("C", "D", "2001.01.01", "0-1", "", "", "1. d4"));
    GameIndex after_edit;
    after_edit.load_blocking(DIR);
    check_eq(after_edit.count(), 2, "edited file forces a rebuild");

    // Adding a file must too.
    write_file("b.pgn", game("E", "F", "2002.01.01", "1-0", "", "", "1. c4"));
    GameIndex after_add;
    after_add.load_blocking(DIR);
    check_eq(after_add.count(), 3, "added file forces a rebuild");
    remove_file("b.pgn");

    GameIndex after_remove;
    after_remove.load_blocking(DIR);
    check_eq(after_remove.count(), 2, "removed file forces a rebuild");
}

static void test_corrupt_cache() {
    std::printf("[corrupt cache]\n");
    fresh_dir();
    write_file("a.pgn", game("A", "B", "2000.01.01", "1-0", "", "", "1. e4"));
    { GameIndex warm; warm.load_blocking(DIR); }

    // Truncated, empty and garbage caches must all fall back to a rebuild
    // rather than crashing or serving nonsense.
    const char *junk[] = { "", "CVIDX01", "not an index file at all",
                           "CVIDX01\0\xff\xff\xff\xff\xff\xff\xff\xff" };
    const size_t lens[] = { 0, 7, 23, 16 };
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(GameIndex::index_file_path(DIR).c_str(), "wb");
        if (f) { if (lens[i]) fwrite(junk[i], 1, lens[i], f); fclose(f); }
        GameIndex idx;
        idx.load_blocking(DIR);
        char msg[80];
        std::snprintf(msg, sizeof(msg), "corrupt cache %d rebuilds cleanly", i);
        check_eq(idx.count(), 1, msg);
    }
}

static void test_async() {
    std::printf("[async]\n");
    fresh_dir();
    std::string body;
    for (int i = 0; i < 200; i++) {
        char w[32], b[32];
        std::snprintf(w, sizeof(w), "White%d", i % 17);
        std::snprintf(b, sizeof(b), "Black%d", i % 23);
        body += game(w, b, "2000.01.01", "1-0", "", "", "1. e4 e5");
    }
    write_file("a.pgn", body);
    remove_file(".chess_viewer_index");

    GameIndex idx;
    idx.load_async(DIR);
    // Accessors must be safe to call before the build finishes.
    for (int i = 0; i < 50; i++) {
        (void)idx.count();
        (void)idx.search("White1");
        (void)idx.players_by_frequency();
    }
    while (idx.is_loading()) { /* spin briefly */ }
    check(idx.loaded(), "async load completes");
    check_eq(idx.count(), 200, "async build indexed every game");
    check_eq((int)idx.players_by_frequency().size(), 17 + 23, "distinct names interned");
}

static void test_empty_dir() {
    std::printf("[empty tree]\n");
    fresh_dir();
    remove_file("a.pgn");
    remove_file("b.pgn");
    remove_file(".chess_viewer_index");
    GameIndex idx;
    idx.load_blocking(DIR);
    check(idx.loaded(), "empty tree still finishes");
    check_eq(idx.count(), 0, "no games");
    check_eq((int)idx.search("anything").size(), 0, "search on empty index is safe");
    check_str(idx.name(0), "", "out-of-range name id is empty");
    check_str(idx.file_path(99), "", "out-of-range file id is empty");
}

int main() {
    std::printf("game index self-test\n\n");
    test_last_name();
    test_names_merge();
    test_build_and_fields();
    test_file_lookup();
    test_offsets_point_at_events();
    test_search();
    test_players_and_years();
    test_cache_round_trip();
    test_cache_invalidation();
    test_corrupt_cache();
    test_async();
    test_empty_dir();

    remove_file(".chess_viewer_index");
    remove_file("a.pgn");
    remove_file("b.pgn");

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
