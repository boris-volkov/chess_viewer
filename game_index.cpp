#include "game_index.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#define FTELL64(f) _ftelli64(f)
#else
#include <dirent.h>
#include <sys/stat.h>
#define FTELL64(f) ftello64(f)
#endif

namespace {

const char  CACHE_MAGIC[8] = {'C','V','I','D','X','0','2','\0'};
const char *CACHE_NAME     = ".chess_viewer_index";

#ifdef _WIN32
const char SEP = '\\';
#else
const char SEP = '/';
#endif

std::string join(const std::string &dir, const std::string &name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
    return dir + SEP + name;
}

bool has_pgn_ext(const std::string &n) {
    if (n.size() < 4) return false;
    std::string tail = n.substr(n.size() - 4);
    for (char &c : tail) c = (char)tolower((unsigned char)c);
    return tail == ".pgn";
}

// Value of a PGN header tag, e.g. [White "Kasparov, G"] -> Kasparov, G.
// Returns false when the line is some other tag entirely.
bool tag_value(const char *line, const char *tag, std::string &out) {
    size_t n = strlen(tag);
    if (line[0] != '[') return false;
    if (strncmp(line + 1, tag, n) != 0) return false;
    if (line[1 + n] != ' ' && line[1 + n] != '"') return false;
    const char *q1 = strchr(line, '"');
    if (!q1) return false;
    const char *q2 = strchr(q1 + 1, '"');
    if (!q2) return false;
    out.assign(q1 + 1, (size_t)(q2 - q1 - 1));
    return true;
}

short parse_year(const std::string &date) {
    // PGN dates are "YYYY.MM.DD", often with ?? for unknown parts.
    if (date.size() < 4) return 0;
    int y = 0;
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)date[i])) return 0;
        y = y * 10 + (date[i] - '0');
    }
    return (short)y;
}

short parse_elo(const std::string &s) {
    if (s.empty()) return 0;
    int v = atoi(s.c_str());
    if (v < 0 || v > 4000) return 0;
    return (short)v;
}

char parse_result(const std::string &s) {
    if (s == "1-0")     return 'W';
    if (s == "0-1")     return 'B';
    if (s == "1/2-1/2") return 'D';
    return '?';
}

// Interning pool. Player names repeat enormously — 419,617 games across 34,763
// distinct names in the bundled collection — so ids cost a fraction of strings.
struct NamePool {
    std::vector<std::string> names;
    std::unordered_map<std::string, int> lookup;
    int intern(const std::string &s) {
        if (s.empty()) return -1;
        auto it = lookup.find(s);
        if (it != lookup.end()) return it->second;
        int id = (int)names.size();
        names.push_back(s);
        lookup.emplace(s, id);
        return id;
    }
};

void put_u32(FILE *f, unsigned int v) { fwrite(&v, sizeof(v), 1, f); }
void put_u64(FILE *f, unsigned long long v) { fwrite(&v, sizeof(v), 1, f); }
void put_str(FILE *f, const std::string &s) {
    unsigned int n = (unsigned int)s.size();
    put_u32(f, n);
    if (n) fwrite(s.data(), 1, n, f);
}
bool get_u32(FILE *f, unsigned int &v) { return fread(&v, sizeof(v), 1, f) == 1; }
bool get_u64(FILE *f, unsigned long long &v) { return fread(&v, sizeof(v), 1, f) == 1; }
bool get_str(FILE *f, std::string &s) {
    unsigned int n = 0;
    if (!get_u32(f, n)) return false;
    if (n > (1u << 20)) return false;               // corrupt length
    s.assign(n, '\0');
    return n == 0 || fread(&s[0], 1, n, f) == n;
}

} // namespace

GameIndex::~GameIndex() {
    if (thread_.joinable()) thread_.join();
}

std::string GameIndex::last_name(const std::string &full) {
    size_t b = full.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    size_t comma = full.find(',', b);
    if (comma != std::string::npos) {
        size_t e = comma;
        while (e > b && isspace((unsigned char)full[e - 1])) e--;
        return full.substr(b, e - b);
    }
    // No comma: take the last whitespace-separated word.
    size_t e = full.find_last_not_of(" \t");
    if (e == std::string::npos) return std::string();
    size_t sp = full.find_last_of(" \t", e);
    size_t start = (sp == std::string::npos) ? b : sp + 1;
    return full.substr(start, e - start + 1);
}

std::string GameIndex::index_file_path(const std::string &games_dir) {
    return join(games_dir, CACHE_NAME);
}

const IndexEntry &GameIndex::entry(int i) const {
    static const IndexEntry empty;
    if (!loaded() || i < 0 || i >= (int)entries_.size()) return empty;
    return entries_[(size_t)i];
}

const std::string &GameIndex::name(int name_id) const {
    static const std::string empty;
    if (!loaded() || name_id < 0 || name_id >= (int)names_.size()) return empty;
    return names_[(size_t)name_id];
}

const std::string &GameIndex::file_path(int file_id) const {
    static const std::string empty;
    if (!loaded() || file_id < 0 || file_id >= (int)files_.size()) return empty;
    return files_[(size_t)file_id];
}

std::string GameIndex::full_path(const IndexEntry &e) const {
    const std::string &rel = file_path(e.file_id);
    if (rel.empty()) return std::string();
    return join(base_dir_, rel);
}

int GameIndex::find_name(const std::string &n) const {
    if (!loaded()) return -1;
    for (size_t i = 0; i < names_.size(); i++)
        if (names_[i] == n) return (int)i;
    return -1;
}

void GameIndex::load_async(const std::string &games_dir) {
    bool expected = false;
    if (!loading_.compare_exchange_strong(expected, true)) return;   // already running
    if (loaded()) { loading_.store(false); return; }
    thread_ = std::thread(&GameIndex::do_load, this, games_dir);
}

void GameIndex::load_blocking(const std::string &games_dir) {
    bool expected = false;
    if (!loading_.compare_exchange_strong(expected, true)) return;
    do_load(games_dir);
}

void GameIndex::list_pgn(const std::string &dir, const std::string &base,
                         std::vector<std::string> &rel_out) {
#ifdef _WIN32
    std::string pattern = join(dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        std::string full = join(dir, n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            list_pgn(full, base, rel_out);
        } else if (has_pgn_ext(n)) {
            rel_out.push_back(full.substr(base.size() + 1));
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string n = ent->d_name;
        if (n == "." || n == "..") continue;
        std::string full = join(dir, n);
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) list_pgn(full, base, rel_out);
        else if (has_pgn_ext(n)) rel_out.push_back(full.substr(base.size() + 1));
    }
    closedir(d);
#endif
}

unsigned long long GameIndex::fingerprint(const std::vector<std::string> &full_paths) {
    // Sum of sizes mixed with the count. Enough to notice a PGN being edited,
    // added or removed without stat-ing 419k individual games.
    unsigned long long fp = 1469598103934665603ULL;
    fp ^= (unsigned long long)full_paths.size() * 1099511628211ULL;
    for (const auto &p : full_paths) {
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long long sz = ftell(f);
        fclose(f);
        fp = (fp ^ (unsigned long long)sz) * 1099511628211ULL;
    }
    return fp;
}

void GameIndex::do_load(std::string games_dir) {
    base_dir_ = games_dir;
    progress_.store(0, std::memory_order_relaxed);

    std::vector<std::string> rel;
    list_pgn(games_dir, games_dir, rel);
    std::sort(rel.begin(), rel.end());

    std::vector<std::string> full;
    full.reserve(rel.size());
    for (const auto &r : rel) full.push_back(join(games_dir, r));
    unsigned long long fp = fingerprint(full);

    if (read_cache(index_file_path(games_dir), fp)) {
        loaded_.store(true, std::memory_order_release);
        loading_.store(false, std::memory_order_release);
        return;
    }

    NamePool pool;
    std::vector<IndexEntry> built;
    built.reserve(400000);

    for (size_t fi = 0; fi < rel.size(); fi++) {
        FILE *f = fopen(full[fi].c_str(), "rb");
        if (!f) continue;
        char line[1024];
        IndexEntry cur;
        std::string white, black, date, result, welo, belo;
        bool have = false;
        long long pos = 0;
        while (fgets(line, sizeof(line), f)) {
            long long line_start = pos;
            pos = FTELL64(f);
            if (line[0] != '[') continue;
            std::string v;
            if (tag_value(line, "Event", v)) {
                if (have) {
                    cur.white_id  = pool.intern(last_name(white));
                    cur.black_id  = pool.intern(last_name(black));
                    cur.year      = parse_year(date);
                    cur.result    = parse_result(result);
                    cur.white_elo = parse_elo(welo);
                    cur.black_elo = parse_elo(belo);
                    built.push_back(cur);
                    progress_.store((int)built.size(), std::memory_order_relaxed);
                }
                cur = IndexEntry();
                cur.file_id = (int)fi;
                cur.offset  = line_start;
                white.clear(); black.clear(); date.clear();
                result.clear(); welo.clear(); belo.clear();
                have = true;
            } else if (tag_value(line, "WhiteElo", v)) { welo = v; }
            else if (tag_value(line, "BlackElo", v))   { belo = v; }
            else if (tag_value(line, "White", v))      { white = v; }
            else if (tag_value(line, "Black", v))      { black = v; }
            else if (tag_value(line, "Date", v))       { date = v; }
            else if (tag_value(line, "Result", v))     { result = v; }
        }
        if (have) {
            cur.white_id  = pool.intern(last_name(white));
            cur.black_id  = pool.intern(last_name(black));
            cur.year      = parse_year(date);
            cur.result    = parse_result(result);
            cur.white_elo = parse_elo(welo);
            cur.black_elo = parse_elo(belo);
            built.push_back(cur);
        }
        fclose(f);
        progress_.store((int)built.size(), std::memory_order_relaxed);
    }

    entries_ = std::move(built);
    names_   = std::move(pool.names);
    files_   = std::move(rel);

    write_cache(index_file_path(games_dir), fp);

    loaded_.store(true, std::memory_order_release);
    loading_.store(false, std::memory_order_release);
}

bool GameIndex::write_cache(const std::string &path, unsigned long long fp) const {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;                       // read-only install: not fatal
    fwrite(CACHE_MAGIC, 1, sizeof(CACHE_MAGIC), f);
    put_u64(f, fp);

    put_u32(f, (unsigned int)files_.size());
    for (const auto &s : files_) put_str(f, s);
    put_u32(f, (unsigned int)names_.size());
    for (const auto &s : names_) put_str(f, s);

    put_u32(f, (unsigned int)entries_.size());
    for (const auto &e : entries_) {
        // Written field by field rather than as a struct blob, so the file does
        // not depend on this compiler's padding choices.
        put_u32(f, (unsigned int)e.file_id);
        put_u64(f, (unsigned long long)e.offset);
        put_u32(f, (unsigned int)e.white_id);
        put_u32(f, (unsigned int)e.black_id);
        put_u32(f, (unsigned int)(unsigned short)e.year);
        put_u32(f, (unsigned int)(unsigned short)e.white_elo);
        put_u32(f, (unsigned int)(unsigned short)e.black_elo);
        put_u32(f, (unsigned int)(unsigned char)e.result);
    }
    fclose(f);
    return true;
}

bool GameIndex::read_cache(const std::string &path, unsigned long long want_fp) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[sizeof(CACHE_MAGIC)];
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, CACHE_MAGIC, sizeof(magic)) != 0) { fclose(f); return false; }

    unsigned long long fp = 0;
    if (!get_u64(f, fp) || fp != want_fp) { fclose(f); return false; }  // stale

    unsigned int n = 0;
    std::vector<std::string> files, names;
    std::vector<IndexEntry> entries;

    if (!get_u32(f, n) || n > 1000000u) { fclose(f); return false; }
    files.resize(n);
    for (unsigned int i = 0; i < n; i++)
        if (!get_str(f, files[i])) { fclose(f); return false; }

    if (!get_u32(f, n) || n > 5000000u) { fclose(f); return false; }
    names.resize(n);
    for (unsigned int i = 0; i < n; i++)
        if (!get_str(f, names[i])) { fclose(f); return false; }

    if (!get_u32(f, n) || n > 50000000u) { fclose(f); return false; }
    entries.resize(n);
    for (unsigned int i = 0; i < n; i++) {
        unsigned int a = 0, b = 0, c = 0, d = 0, e2 = 0, g = 0, h = 0;
        unsigned long long off = 0;
        if (!get_u32(f, a) || !get_u64(f, off) || !get_u32(f, b) || !get_u32(f, c) ||
            !get_u32(f, d) || !get_u32(f, e2) || !get_u32(f, g) || !get_u32(f, h)) {
            fclose(f); return false;
        }
        IndexEntry &x = entries[i];
        x.file_id   = (int)a;
        x.offset    = (long long)off;
        x.white_id  = (int)b;
        x.black_id  = (int)c;
        x.year      = (short)(unsigned short)d;
        x.white_elo = (short)(unsigned short)e2;
        x.black_elo = (short)(unsigned short)g;
        x.result    = (char)(unsigned char)h;
    }
    fclose(f);

    files_   = std::move(files);
    names_   = std::move(names);
    entries_ = std::move(entries);
    progress_.store((int)entries_.size(), std::memory_order_relaxed);
    return true;
}

std::vector<int> GameIndex::search(const std::string &query, int limit) const {
    std::vector<int> out;
    if (!loaded() || query.empty()) return out;

    std::string q = query;
    for (char &c : q) c = (char)toupper((unsigned char)c);

    auto icontains = [&](const std::string &s) {
        if (s.size() < q.size()) return false;
        for (size_t i = 0; i + q.size() <= s.size(); i++) {
            size_t j = 0;
            while (j < q.size() && (char)toupper((unsigned char)s[i + j]) == q[j]) j++;
            if (j == q.size()) return true;
        }
        return false;
    };

    // Names are matched once each, not once per game: with 419k games over
    // 34k names, testing every entry's strings would be ~12x the work.
    std::vector<char> name_hit(names_.size(), 0);
    for (size_t i = 0; i < names_.size(); i++) name_hit[i] = icontains(names_[i]) ? 1 : 0;

    for (size_t i = 0; i < entries_.size(); i++) {
        const IndexEntry &e = entries_[i];
        bool hit = (e.white_id >= 0 && name_hit[(size_t)e.white_id]) ||
                   (e.black_id >= 0 && name_hit[(size_t)e.black_id]);
        if (!hit && e.year > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", (int)e.year);
            hit = icontains(buf);
        }
        if (hit) {
            out.push_back((int)i);
            if (limit > 0 && (int)out.size() >= limit) break;
        }
    }
    return out;
}

std::vector<int> GameIndex::games_of_player(int name_id) const {
    std::vector<int> out;
    if (!loaded() || name_id < 0) return out;
    for (size_t i = 0; i < entries_.size(); i++)
        if (entries_[i].white_id == name_id || entries_[i].black_id == name_id)
            out.push_back((int)i);
    return out;
}

std::vector<std::pair<int, int>> GameIndex::players_by_frequency() const {
    std::vector<std::pair<int, int>> out;
    if (!loaded()) return out;
    std::vector<int> counts(names_.size(), 0);
    for (const auto &e : entries_) {
        if (e.white_id >= 0) counts[(size_t)e.white_id]++;
        if (e.black_id >= 0) counts[(size_t)e.black_id]++;
    }
    out.reserve(names_.size());
    for (size_t i = 0; i < names_.size(); i++)
        if (counts[i] > 0) out.emplace_back((int)i, counts[i]);
    std::sort(out.begin(), out.end(),
              [this](const std::pair<int,int> &a, const std::pair<int,int> &b) {
                  if (a.second != b.second) return a.second > b.second;
                  return names_[(size_t)a.first] < names_[(size_t)b.first];
              });
    return out;
}

std::vector<int> GameIndex::games_in_file(int file_id) const {
    std::vector<int> out;
    if (!loaded() || file_id < 0) return out;
    for (size_t i = 0; i < entries_.size(); i++)
        if (entries_[i].file_id == file_id) out.push_back((int)i);
    return out;
}

int GameIndex::find_file(const std::string &rel_path) const {
    if (!loaded()) return -1;
    // The catalog builds paths with the platform separator while the index
    // stored whatever the directory walk produced, so compare both normalised.
    auto norm = [](std::string s) {
        for (char &c : s) if (c == '/') c = '\\';
        return s;
    };
    std::string want = norm(rel_path);
    for (size_t i = 0; i < files_.size(); i++)
        if (norm(files_[i]) == want) return (int)i;
    return -1;
}

std::vector<int> GameIndex::years() const {
    std::vector<int> out;
    if (!loaded()) return out;
    std::map<int, int> seen;
    for (const auto &e : entries_) if (e.year > 0) seen[e.year]++;
    for (auto it = seen.rbegin(); it != seen.rend(); ++it) out.push_back(it->first);
    return out;
}

std::vector<int> GameIndex::games_in_year(int year) const {
    std::vector<int> out;
    if (!loaded()) return out;
    for (size_t i = 0; i < entries_.size(); i++)
        if (entries_[i].year == year) out.push_back((int)i);
    return out;
}
