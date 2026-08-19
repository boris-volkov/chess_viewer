// Test harness for settings persistence.
// Includes the real translation unit (with its main renamed) so load_settings /
// save_settings under test are the ones that actually ship.
//
// Every test points settings_path_buf at a scratch file first — the real
// settings.txt sits next to the executable, and a test run must never clobber
// the preferences of whoever is using the program.
#define SDL_MAIN_HANDLED
#define main chess_viewer_orig_main
#include "chess_viewer.cpp"
#undef main

#include <cstdio>
#include <string>

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

static const char *TEST_PATH = "settings_test_scratch.txt";

static void use_scratch_file() {
    std::snprintf(settings_path_buf, sizeof(settings_path_buf), "%s", TEST_PATH);
}

static void remove_scratch_file() {
    std::remove(TEST_PATH);
}

static void write_raw(const char *contents) {
    FILE *f = std::fopen(TEST_PATH, "w");
    if (!f) { std::printf("  FAIL: could not open scratch file\n"); failures++; return; }
    std::fputs(contents, f);
    std::fclose(f);
}

static void set_all(int elos, int uncol, int def, int white, int delay) {
    show_elos          = elos;
    uncolored_mode     = uncol;
    show_defense_lines = def;
    view_from_white    = white;
    move_delay_ms      = delay;
}

// ── Tests ────────────────────────────────────────────────────────────────────

static void test_round_trip() {
    std::printf("[round trip]\n");
    use_scratch_file();
    remove_scratch_file();

    set_all(1, 1, 1, 0, 3500);
    save_settings();

    // Scramble every value, so a load that silently does nothing cannot pass.
    set_all(0, 0, 0, 1, MOVE_DELAY_MS);
    load_settings();

    check_eq(show_elos,          1,    "show_elos restored");
    check_eq(uncolored_mode,     1,    "uncolored_mode restored");
    check_eq(show_defense_lines, 1,    "show_defense_lines restored");
    check_eq(view_from_white,    0,    "view_from_white restored");
    check_eq(move_delay_ms,      3500, "move_delay_ms restored");

    // And the other way round, so we are not just reading back constants.
    set_all(0, 0, 0, 1, 8000);
    save_settings();
    set_all(1, 1, 1, 0, MOVE_DELAY_MS);
    load_settings();

    check_eq(show_elos,          0,    "show_elos restored (inverted)");
    check_eq(uncolored_mode,     0,    "uncolored_mode restored (inverted)");
    check_eq(show_defense_lines, 0,    "show_defense_lines restored (inverted)");
    check_eq(view_from_white,    1,    "view_from_white restored (inverted)");
    check_eq(move_delay_ms,      8000, "move_delay_ms restored (inverted)");

    remove_scratch_file();
}

static void test_missing_file_keeps_defaults() {
    std::printf("[missing file]\n");
    use_scratch_file();
    remove_scratch_file();

    set_all(1, 0, 1, 0, 7000);
    load_settings();          // no file on disk — must be a no-op, not a reset

    check_eq(show_elos,          1,    "show_elos untouched");
    check_eq(uncolored_mode,     0,    "uncolored_mode untouched");
    check_eq(show_defense_lines, 1,    "show_defense_lines untouched");
    check_eq(view_from_white,    0,    "view_from_white untouched");
    check_eq(move_delay_ms,      7000, "move_delay_ms untouched");
}

static void test_unknown_keys_ignored() {
    std::printf("[unknown keys]\n");
    use_scratch_file();
    // A file written by a hypothetical newer build: keys this build has never
    // heard of must be skipped without disturbing the ones it does know.
    write_raw("show_elos 1\n"
              "future_option 42\n"
              "uncolored_mode 1\n"
              "another_unknown 0\n"
              "move_delay_ms 1500\n");

    set_all(0, 0, 0, 1, MOVE_DELAY_MS);
    load_settings();

    check_eq(show_elos,      1,    "known key before unknown applied");
    check_eq(uncolored_mode, 1,    "known key between unknowns applied");
    check_eq(move_delay_ms,  1500, "known key after unknowns applied");
    remove_scratch_file();
}

static void test_absent_key_keeps_current() {
    std::printf("[absent key]\n");
    use_scratch_file();
    // A file written by an older build that predates some settings: the missing
    // ones keep whatever the program already had.
    write_raw("show_elos 1\n");

    set_all(0, 1, 1, 0, 6500);
    load_settings();

    check_eq(show_elos,          1,    "present key applied");
    check_eq(uncolored_mode,     1,    "absent key keeps current value");
    check_eq(show_defense_lines, 1,    "absent key keeps current value");
    check_eq(view_from_white,    0,    "absent key keeps current value");
    check_eq(move_delay_ms,      6500, "absent key keeps current value");
    remove_scratch_file();
}

static void test_delay_clamped() {
    std::printf("[speed clamping]\n");
    use_scratch_file();

    // Below the floor — including zero and negative, which would otherwise wedge
    // playback into firing moves as fast as the loop spins.
    const int too_low[] = {0, -1, -999999, MOVE_DELAY_MIN_MS - 1};
    for (int i = 0; i < (int)(sizeof(too_low) / sizeof(too_low[0])); i++) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "move_delay_ms %d\n", too_low[i]);
        write_raw(buf);
        move_delay_ms = MOVE_DELAY_MS;
        load_settings();
        char msg[96];
        std::snprintf(msg, sizeof(msg), "%d clamped up to minimum", too_low[i]);
        check_eq(move_delay_ms, MOVE_DELAY_MIN_MS, msg);
    }

    // Above the ceiling.
    const int too_high[] = {MOVE_DELAY_MAX_MS + 1, 100000, 999999999};
    for (int i = 0; i < (int)(sizeof(too_high) / sizeof(too_high[0])); i++) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "move_delay_ms %d\n", too_high[i]);
        write_raw(buf);
        move_delay_ms = MOVE_DELAY_MS;
        load_settings();
        char msg[96];
        std::snprintf(msg, sizeof(msg), "%d clamped down to maximum", too_high[i]);
        check_eq(move_delay_ms, MOVE_DELAY_MAX_MS, msg);
    }

    // Exact bounds must survive untouched.
    write_raw("move_delay_ms 500\n");
    move_delay_ms = MOVE_DELAY_MS;
    load_settings();
    check_eq(move_delay_ms, MOVE_DELAY_MIN_MS, "minimum preserved exactly");

    write_raw("move_delay_ms 20000\n");
    move_delay_ms = MOVE_DELAY_MS;
    load_settings();
    check_eq(move_delay_ms, MOVE_DELAY_MAX_MS, "maximum preserved exactly");

    remove_scratch_file();
}

static void test_malformed_file() {
    std::printf("[malformed input]\n");
    use_scratch_file();

    const char *junk[] = {
        "",                                   // empty
        "\n\n\n",                             // blank lines only
        "show_elos",                          // key with no value
        "show_elos notanumber\n",             // non-numeric value
        "garbage garbage garbage\n",
        "show_elos 1\nuncolored_mode",        // truncated mid-file
        "\0\0\0",                             // NULs
    };
    for (int i = 0; i < (int)(sizeof(junk) / sizeof(junk[0])); i++) {
        write_raw(junk[i]);
        set_all(0, 0, 0, 1, 4000);
        load_settings();      // must return, not hang or crash
        checks++;
        // Values must still be within legal bounds whatever the file contained.
        if (move_delay_ms < MOVE_DELAY_MIN_MS || move_delay_ms > MOVE_DELAY_MAX_MS) {
            failures++;
            std::printf("  FAIL: malformed input %d left delay out of range (%d)\n",
                        i, move_delay_ms);
        }
    }
    std::printf("  survived %d malformed inputs\n",
                (int)(sizeof(junk) / sizeof(junk[0])));
    remove_scratch_file();
}

static void test_no_path_is_safe() {
    std::printf("[unset path]\n");
    // Before main() resolves the exe directory the path buffer is empty; both
    // calls must no-op rather than touching a relative file in the cwd.
    settings_path_buf[0] = '\0';
    set_all(1, 0, 1, 0, 9000);
    save_settings();
    load_settings();
    check_eq(move_delay_ms, 9000, "values untouched with no path set");
    FILE *f = std::fopen("", "r");
    check(f == nullptr, "no stray file created");
    if (f) std::fclose(f);
}

static void test_path_resolution() {
    std::printf("[path resolution]\n");
    char games[1024];

    // The app is fullscreen and cannot be driven by synthetic keystrokes, so
    // this is the only way to confirm settings.txt actually lands beside the
    // executable rather than wherever the program happened to be launched from.
    resolve_app_paths("C:\\apps\\chess\\", games, sizeof(games));
    check(std::string(settings_path_buf) == "C:\\apps\\chess\\settings.txt",
          "settings.txt resolves next to the exe");
    check(std::string(fen_save_path_buf) == "C:\\apps\\chess\\saved_positions.fen",
          "saved_positions.fen resolves next to the exe");
    check(std::string(pieces_dir_buf) == "C:\\apps\\chess\\pieces/",
          "pieces/ resolves next to the exe");
    check(std::string(games) == "C:\\apps\\chess\\games/players",
          "games dir resolves next to the exe");

    // Empty base (SDL_GetBasePath failed): fall back to plain relative names
    // rather than producing something malformed.
    resolve_app_paths("", games, sizeof(games));
    check(std::string(settings_path_buf) == "settings.txt", "empty base gives bare filename");
    check(std::string(games) == "games/players",            "empty base gives bare games dir");

    // Null base must not crash.
    resolve_app_paths(nullptr, games, sizeof(games));
    check(std::string(settings_path_buf) == "settings.txt", "null base treated as empty");

    // Null out-pointer must not crash either.
    resolve_app_paths("/tmp/", nullptr, 0);
    check(std::string(settings_path_buf) == "/tmp/settings.txt", "null games_dir_out tolerated");
}

int main() {
    std::printf("settings persistence self-test\n\n");
    test_path_resolution();
    test_round_trip();
    test_missing_file_keeps_defaults();
    test_unknown_keys_ignored();
    test_absent_key_keeps_current();
    test_delay_clamped();
    test_malformed_file();
    test_no_path_is_safe();
    remove_scratch_file();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
