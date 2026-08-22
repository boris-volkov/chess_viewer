#pragma once
#include <string>
#include <vector>

// A tree of opening positions over the indexed games, so the catalog can answer
// "which games reached this position" and "what was played from here".
//
// The shape is what makes it cheap. Positions are shared enormously — measured
// on the bundled collection, 419,617 games occupy only 187,115 distinct
// positions at ply 20, and 410,542 nodes over the first 16 plies. Storing a
// game list per node would cost more than the games themselves, so instead the
// games are sorted into tree order once, which makes every node's games a
// contiguous range: two ints, whatever the node's size.
//
// The book does not know the rules of chess. Positions arrive already hashed
// from whoever is driving the walk (see opening_build in chess_viewer.cpp),
// because the move logic lives there and works on globals.
struct OpeningNode {
    unsigned long long key = 0;   // position hash after `depth` plies
    int   first_game  = 0;        // [first_game, first_game+game_count) into order()
    int   game_count  = 0;
    // Offset into OpeningBook's child table, not a node index. Nodes are stored
    // depth-first so each subtree is contiguous — which is what makes a node's
    // games a range — but that same ordering scatters its children, since DFS
    // emits a child's whole subtree before the next child. So children get their
    // own array.
    int   first_child = -1;
    short child_count = 0;
    short depth       = 0;
    char  move[8]     = {0};      // SAN that reached here, truncated
};

class OpeningBook {
public:
    // Openings are identified within the first few moves; 16 plies covers every
    // named line while keeping the node table to about 14MB. Raising it costs
    // roughly 8MB per two plies (measured), lowering it saves the same.
    static const int MAX_PLIES = 16;

    bool ready() const { return ready_; }
    int  node_count() const { return (int)nodes_.size(); }
    int  game_count() const { return (int)order_.size(); }

    // Root is the starting position, and holds every game that was walked.
    int root() const { return nodes_.empty() ? -1 : 0; }
    const OpeningNode &node(int i) const;

    // Child of `parent` whose position is `key`, or -1. The caller plays a move,
    // hashes the result, and looks it up here — a linear scan over the handful
    // of moves actually played from a position, so no global lookup is needed.
    int child_with_key(int parent, unsigned long long key) const;

    // Child node indices of `parent`, most-played first.
    std::vector<int> children(int parent) const;

    // Game ids under `node`, which is every game that passed through it.
    std::vector<int> games(int node) const;

    // ── Building ────────────────────────────────────────────────────────────
    // Walked one game at a time: `keys` are the position hashes after each ply
    // and `moves` the SAN that produced them, both up to MAX_PLIES long.
    void begin_build();
    void add_game(int game_id, const unsigned long long *keys,
                  const char (*moves)[8], int n);
    void finish_build();

    // Cache. The fingerprint is the index's, so an edited collection rebuilds
    // the book rather than answering from a stale one.
    bool save(const std::string &path, unsigned long long fingerprint) const;
    bool load(const std::string &path, unsigned long long fingerprint);
    static std::string book_path(const std::string &games_dir);

    void clear();

private:
    std::vector<OpeningNode> nodes_;    // depth-first order: a subtree is contiguous
    std::vector<int>         order_;    // game ids, sorted into that same order
    std::vector<int>         kids_;     // child node indices, contiguous per node
    bool ready_ = false;

    // Build-time only, discarded by finish_build().
    struct BuildNode {
        unsigned long long key = 0;
        int    parent = -1;
        short  depth = 0;
        char   move[8] = {0};
        int    hits = 0;               // games through here, for ordering children
        std::vector<int> kids;
    };
    std::vector<BuildNode> build_;
    std::vector<int>       game_ids_;      // parallel to deepest_
    std::vector<int>       deepest_;       // deepest build node each game reached
};
