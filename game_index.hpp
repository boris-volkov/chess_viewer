#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>

// Persistent per-game index over a tree of PGN files.
//
// Unlike go_station's equivalent, an entry addresses a game *inside* a file
// rather than a file: the collections here pack 6 to 59,066 games per PGN, so
// a file is not a unit anyone wants to browse. Each entry carries the byte
// offset of its [Event line, which is what lets the catalog show 419,617
// individual games while they stay packed on disk.
//
// Layout is deliberately flat and fixed-width. Measured on the bundled
// collection, a string-per-field entry costs ~70MB of heap across 419k games;
// interning the 34,763 distinct player names and storing ids brings the same
// data to ~11MB.
struct IndexEntry {
    int       file_id   = -1;   // into GameIndex::file_path()
    long long offset    = 0;    // byte offset of this game's [Event line
    int       white_id  = -1;   // into GameIndex::name()
    int       black_id  = -1;
    short     year      = 0;    // 0 = unknown
    short     white_elo = 0;    // 0 = unknown
    short     black_elo = 0;
    char      result    = '?';  // 'W' white won, 'B' black won, 'D' draw, '?' unknown
};

class GameIndex {
public:
    ~GameIndex();

    // Build or load in a background thread. Safe to call repeatedly; only the
    // first call starts work. Every accessor below is safe to call while this
    // runs — they report empty until it finishes.
    void load_async(const std::string &games_dir);

    // Build synchronously on the calling thread. For tests and tools.
    void load_blocking(const std::string &games_dir);

    bool loaded() const     { return loaded_.load(std::memory_order_acquire); }
    bool is_loading() const { return loading_.load(std::memory_order_acquire); }

    // Games scanned so far — drives a "Building index..." line in the catalog.
    int progress() const    { return progress_.load(std::memory_order_relaxed); }

    int count() const { return loaded() ? (int)entries_.size() : 0; }

    // Valid only once loaded(). Out-of-range ids yield an empty string / a
    // default entry rather than undefined behaviour.
    const IndexEntry  &entry(int i) const;
    const std::string &name(int name_id) const;
    const std::string &file_path(int file_id) const;

    // Full filesystem path for an entry's file.
    std::string full_path(const IndexEntry &e) const;

    // Case-insensitive substring match over player names, year and result.
    // Returns entry indices, capped at `limit` (<=0 means no cap) because a
    // one-letter query legitimately matches most of the collection.
    std::vector<int> search(const std::string &query, int limit = 0) const;

    // Entry indices for every game a name played, either colour.
    std::vector<int> games_of_player(int name_id) const;

    // Distinct player name ids with their game counts, most games first.
    std::vector<std::pair<int, int>> players_by_frequency() const;

    // Entry indices for every game held in one file, in file order.
    std::vector<int> games_in_file(int file_id) const;

    // File id for a path relative to the index root, or -1.
    int find_file(const std::string &rel_path) const;

    // Distinct years present, descending.
    std::vector<int> years() const;
    std::vector<int> games_in_year(int year) const;

    // Name id for an exact name, or -1.
    int find_name(const std::string &name) const;

    // Surname portion of a PGN name tag: the text before the comma
    // ("Kasparov, Garry" -> "Kasparov"), or the last word when there is no
    // comma. Names are stored this way, which shortens rows and merges the
    // several spellings the source files use for one player -- "Ivanchuk,V"
    // and "Ivanchuk, Vassily" were separate entries before.
    //
    // This is the single definition of the rule; chess_viewer's set_last_name
    // defers to it so the catalog and the playback caption cannot disagree.
    static std::string last_name(const std::string &full);

    // Root the index was built over, and the fingerprint that keys its cache.
    // The opening book hangs off both: it lives beside the index and has to go
    // stale on exactly the same edits.
    const std::string &base_dir() const { return base_dir_; }
    unsigned long long fingerprint_value() const { return fingerprint_; }

    // Where the cache lives, for tests and for reporting.
    static std::string index_file_path(const std::string &games_dir);

private:
    // Publication: the worker fills these, then sets loaded_ with release
    // ordering. Readers acquire loaded_ first, after which the data is
    // immutable and needs no further locking.
    std::vector<IndexEntry>  entries_;
    std::vector<std::string> names_;
    std::vector<std::string> files_;
    std::string              base_dir_;

    std::atomic<bool> loaded_{false};
    std::atomic<bool> loading_{false};
    unsigned long long fingerprint_ = 0;
    std::atomic<int>  progress_{0};
    std::thread       thread_;

    void do_load(std::string games_dir);
    bool read_cache(const std::string &path, unsigned long long want_fingerprint);
    bool write_cache(const std::string &path, unsigned long long fingerprint) const;

    // Sum of file sizes plus file count. Cheap to compute and enough to notice
    // an edited, added or removed PGN without stat-ing every game.
    static unsigned long long fingerprint(const std::vector<std::string> &full_paths);
    static void list_pgn(const std::string &dir, const std::string &base,
                         std::vector<std::string> &rel_out);
};
