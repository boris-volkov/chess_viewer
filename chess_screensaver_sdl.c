// chess_screensaver_sdl.c - A minimal PGN chess viewer using SDL2 for graphical display
// Displays games as a screensaver-like animation with 1-second delays between moves
// Dependencies: SDL2 and SDL2_image (for loading PNG piece images)
// Compile (Linux/Mac): gcc chess_screensaver_sdl.c -o chess_screensaver_sdl -lSDL2 -lSDL2_image
// Windows: Use a setup like MinGW or Visual Studio with SDL2 libs

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define BOARD_SIZE 8
#define SCREEN_SIZE 800
#define MAX_MOVES 8192
#define MOVE_TEXT_LEN 32
#define MOVE_DELAY_MS 5000

char board[BOARD_SIZE][BOARD_SIZE];
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *piece_textures[256] = {NULL};

void init_board() {
    const char *initial[] = {
        "rnbqkbnr",
        "pppppppp",
        "........",
        "........",
        "........",
        "........",
        "PPPPPPPP",
        "RNBQKBNR"
    };
    for (int i = 0; i < BOARD_SIZE; i++) {
        strcpy(board[i], initial[i]);
    }
}

SDL_Texture *get_piece_texture(char piece) {
    if (piece == '.') return NULL;
    unsigned char idx = (unsigned char)piece;
    if (piece_textures[idx]) return piece_textures[idx];

    char letter = tolower(piece);
    const char *color = isupper(piece) ? "lt" : "dt";
    char path[64];
    snprintf(path, sizeof(path), "pieces/Chess_%c%s60.png", letter, color);  // Changed to 60.png

    SDL_Texture *tex = IMG_LoadTexture(renderer, path);
    if (!tex) {
        printf("Failed to load %s: %s\n", path, IMG_GetError());
    }
    piece_textures[idx] = tex;
    return tex;
}

void draw_board() {
    int screen_w = SCREEN_SIZE;
    int screen_h = SCREEN_SIZE;
    if (SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h) != 0) {
        screen_w = SCREEN_SIZE;
        screen_h = SCREEN_SIZE;
    }
    int min_dim = (screen_w < screen_h) ? screen_w : screen_h;
    int square = min_dim / BOARD_SIZE;
    if (square < 1) square = 1;
    int board_px = square * BOARD_SIZE;
    int offset_x = (screen_w - board_px) / 2;
    int offset_y = (screen_h - board_px) / 2;

    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderClear(renderer);

    SDL_Color light = {210, 210, 210, 255};
    SDL_Color dark  = {90,  90,  90, 255};

    for (int row = 0; row < BOARD_SIZE; row++) {  // row 0 = rank 8
        for (int col = 0; col < BOARD_SIZE; col++) {
            SDL_Color colr = ((row + col) % 2 == 0) ? light : dark;
            SDL_SetRenderDrawColor(renderer, colr.r, colr.g, colr.b, colr.a);
            SDL_Rect rect = {offset_x + col * square, offset_y + row * square, square, square};
            SDL_RenderFillRect(renderer, &rect);

            char piece = board[row][col];
            SDL_Texture *tex = get_piece_texture(piece);
            if (tex) {
                SDL_RenderCopy(renderer, tex, NULL, &rect);
            }
        }
    }
    SDL_RenderPresent(renderer);
}

int sign(int x) { return (x > 0) ? 1 : (x < 0) ? -1 : 0; }

int is_path_clear(int from_r, int from_f, int to_r, int to_f) {
    int dr = sign(to_r - from_r);
    int df = sign(to_f - from_f);
    int steps = (dr == 0) ? abs(to_f - from_f) : abs(to_r - from_r);
    for (int i = 1; i < steps; i++) {
        if (board[from_r + i * dr][from_f + i * df] != '.') return 0;
    }
    return 1;
}

int is_valid_move(char piece, int from_r, int from_f, int to_r, int to_f, int is_white, int capture) {
    int dr = abs(to_r - from_r);
    int df = abs(to_f - from_f);
    char p = toupper(piece);
    int dir = is_white ? -1 : 1;  // row direction (board[0] = rank 8)
    char at_to = board[to_r][to_f];
    int is_empty = (at_to == '.');
    int is_enemy = !is_empty && (isupper(at_to) != is_white);

    int movement_valid = 0;

    switch (p) {
        case 'P':
            if (df == 0) {  // advance
                if (capture) return 0;
                if (!is_empty) return 0;
                if (dr == 1 && (to_r - from_r) == dir) movement_valid = 1;
                else if (dr == 2 && ((is_white && from_r == 6) || (!is_white && from_r == 1)) && (to_r - from_r) == 2 * dir)
                    movement_valid = is_path_clear(from_r, from_f, to_r, to_f);
            } else if (df == 1 && dr == 1 && (to_r - from_r) == dir) {
                if (capture && is_enemy) movement_valid = 1;
                else if (capture && is_empty) {  // en passant
                    int ep_rank = is_white ? 3 : 4;  // board row for own pawn rank 5/4 (white/black)
                    if (from_r == ep_rank && board[from_r][to_f] == (is_white ? 'p' : 'P')) movement_valid = 1;  // Check if enemy pawn is there for ep
                }
            }
            break;
        case 'N':
            movement_valid = ((dr == 1 && df == 2) || (dr == 2 && df == 1));
            break;
        case 'B':
            movement_valid = (dr == df && dr > 0 && is_path_clear(from_r, from_f, to_r, to_f));
            break;
        case 'R':
            movement_valid = ((dr == 0 || df == 0) && (dr + df > 0) && is_path_clear(from_r, from_f, to_r, to_f));
            break;
        case 'Q':
            movement_valid = (((dr == df) || (dr == 0 || df == 0)) && (dr + df > 0) && is_path_clear(from_r, from_f, to_r, to_f));
            break;
        case 'K':
            movement_valid = (dr <= 1 && df <= 1 && (dr + df > 0));
            break;
    }

    if (movement_valid) {
        if (capture) return is_enemy || (p == 'P' && is_empty && movement_valid);  // for ep
        else return is_empty;
    }
    return 0;
}

int is_in_check(int is_white) {
    int king_r = -1, king_f = -1;
    char king = is_white ? 'K' : 'k';
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int f = 0; f < BOARD_SIZE; f++) {
            if (board[r][f] == king) {
                king_r = r;
                king_f = f;
                break;
            }
        }
        if (king_r != -1) break;
    }
    if (king_r == -1) return 0;  // No king? Unlikely

    int opponent_is_white = !is_white;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int f = 0; f < BOARD_SIZE; f++) {
            char p = board[r][f];
            if (p == '.' || (isupper(p) != opponent_is_white)) continue;  // Not opponent piece
            if (is_valid_move(p, r, f, king_r, king_f, opponent_is_white, 1)) {
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    int from_r, from_f, to_r, to_f;
    char promo;
} Move;

// Forward declaration
void apply_move(const Move *m, int is_white);

int parse_san(const char *san, int is_white, Move *m) {
    char clean_san[16];
    strcpy(clean_san, san);
    int len = strlen(clean_san);
    if (clean_san[len - 1] == '+' || clean_san[len - 1] == '#') clean_san[--len] = '\0';

    m->promo = '\0';

    if (strcmp(clean_san, "O-O") == 0 || strcmp(clean_san, "0-0") == 0) {
        m->from_r = is_white ? 7 : 0;
        m->from_f = 4;
        m->to_r = m->from_r;
        m->to_f = 6;
        return 1;
    } else if (strcmp(clean_san, "O-O-O") == 0 || strcmp(clean_san, "0-0-0") == 0) {
        m->from_r = is_white ? 7 : 0;
        m->from_f = 4;
        m->to_r = m->from_r;
        m->to_f = 2;
        return 1;
    }

    // Promotion handling
    char *eq = strchr(clean_san, '=');
    if (eq) {
        m->promo = *(eq + 1);
        *eq = '\0';
        len = strlen(clean_san);
    }

    if (len < 2) return 0;
    int to_f = clean_san[len - 2] - 'a';
    int to_r = clean_san[len - 1] - '1';
    to_r = 7 - to_r;  // Invert to match board index (0 = rank 8, 7 = rank 1)
    if (to_r < 0 || to_r >= 8 || to_f < 0 || to_f >= 8) return 0;
    m->to_r = to_r;
    m->to_f = to_f;

    char piece = 'P';
    int hint_start = 0;
    if (isupper(clean_san[0]) && strchr("RNBQK", clean_san[0])) {
        piece = clean_san[0];
        hint_start = 1;
    }

    int capture_pos = 0;
    for (int i = hint_start; i < len - 2; i++) {
        if (clean_san[i] == 'x') {
            capture_pos = i;
            break;
        }
    }
    int capture = (capture_pos > 0);

    int hint_len = (capture_pos > 0 ? capture_pos : len - 2) - hint_start;
    char hint_str[4] = {0};
    if (hint_len > 0) {
        strncpy(hint_str, clean_san + hint_start, hint_len);
    }

    int hl = strlen(hint_str);
    int hint_f = -1, hint_r = -1;
    if (hl == 1) {
        if (islower(hint_str[0])) hint_f = hint_str[0] - 'a';
        else if (isdigit(hint_str[0])) hint_r = 7 - (hint_str[0] - '1');
    } else if (hl == 2) {
        hint_f = hint_str[0] - 'a';
        hint_r = 7 - (hint_str[1] - '1');
    }

    char target_piece = is_white ? piece : tolower(piece);

    struct Candidate {
        int r, f;
    };
    struct Candidate candidates[16];
    int num_candidates = 0;

    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int f = 0; f < BOARD_SIZE; f++) {
            if (board[r][f] == target_piece) {
                if ((hint_f >= 0 && f != hint_f) || (hint_r >= 0 && r != hint_r)) continue;
                if (is_valid_move(target_piece, r, f, to_r, to_f, is_white, capture)) {
                    candidates[num_candidates].r = r;
                    candidates[num_candidates].f = f;
                    num_candidates++;
                }
            }
        }
    }

    if (num_candidates == 0) return 0;
    if (num_candidates == 1) {
        m->from_r = candidates[0].r;
        m->from_f = candidates[0].f;
        return 1;
    }

    // Resolve ambiguity with legality check (no self-check)
    for (int i = 0; i < num_candidates; i++) {
        char temp_board[BOARD_SIZE][BOARD_SIZE];
        memcpy(temp_board, board, sizeof(board));

        Move temp_m = *m;
        temp_m.from_r = candidates[i].r;
        temp_m.from_f = candidates[i].f;
        apply_move(&temp_m, is_white);  // Apply on temp
        if (!is_in_check(is_white)) {
            memcpy(board, temp_board, sizeof(board));  // Restore original board
            m->from_r = candidates[i].r;
            m->from_f = candidates[i].f;
            return 1;
        }
        memcpy(board, temp_board, sizeof(board));  // Restore original board
    }

    return 0;  // No legal move found
}

void apply_move(const Move *m, int is_white) {
    char piece = board[m->from_r][m->from_f];
    char captured = board[m->to_r][m->to_f];

    // En passant capture
    int dir = is_white ? -1 : 1;
    if (toupper(piece) == 'P' && abs(m->from_f - m->to_f) == 1 && captured == '.') {
        board[m->to_r - dir][m->to_f] = '.';  // Remove captured pawn
    }

    // Place the piece (with promotion if applicable)
    board[m->to_r][m->to_f] = m->promo ? (is_white ? toupper(m->promo) : tolower(m->promo)) : piece;
    board[m->from_r][m->from_f] = '.';

    // Castling: move rook
    if (toupper(piece) == 'K' && abs(m->from_f - m->to_f) == 2) {
        int rook_from = (m->to_f > m->from_f) ? 7 : 0;
        int rook_to = (m->to_f > m->from_f) ? 5 : 3;
        char rook = is_white ? 'R' : 'r';
        board[m->from_r][rook_to] = rook;
        board[m->from_r][rook_from] = '.';
    }
}

void clean_line(char *line) {
    char *out = line;
    int in_comment = 0;
    int in_var = 0;
    while (*line) {
        if (*line == '{') in_comment = 1;
        else if (*line == '}') in_comment = 0;
        else if (*line == '(') in_var = 1;
        else if (*line == ')') in_var = 0;
        else if (*line == ';') {
            while (*line && *line != '\n') line++;
            continue;
        } else if (!in_comment && !in_var) {
            *out++ = *line;
        }
        line++;
    }
    *out = '\0';
}

int extract_san_token(const char *token, char *out, size_t out_size) {
    const char *p = token;

    if (isdigit((unsigned char)*p)) {
        const char *q = p;
        while (isdigit((unsigned char)*q)) q++;
        if (*q == '.') {
            p = q;
            while (*p == '.') p++;
        }
    } else {
        while (*p == '.') p++;
    }

    if (*p == '\0') return 0;

    size_t len = strcspn(p, "!?");
    if (len == 0) return 0;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

int is_result_token(const char *san) {
    return (strcmp(san, "1-0") == 0 || strcmp(san, "0-1") == 0 ||
            strcmp(san, "1/2-1/2") == 0 || strcmp(san, "*") == 0);
}

int build_move_list(const char *move_buffer, char moves[][MOVE_TEXT_LEN], int max_moves) {
    char temp_buffer[65536];
    strncpy(temp_buffer, move_buffer, sizeof(temp_buffer) - 1);
    temp_buffer[sizeof(temp_buffer) - 1] = '\0';

    int count = 0;
    char *token = strtok(temp_buffer, " \t\n\r");
    while (token) {
        char san_buf[MOVE_TEXT_LEN];
        if (extract_san_token(token, san_buf, sizeof(san_buf))) {
            if (is_result_token(san_buf)) break;
            if (count < max_moves) {
                strncpy(moves[count], san_buf, MOVE_TEXT_LEN - 1);
                moves[count][MOVE_TEXT_LEN - 1] = '\0';
                count++;
            }
        }
        token = strtok(NULL, " \t\n\r");
    }
    return count;
}

void replay_moves_to_index(char moves[][MOVE_TEXT_LEN], int move_count, int index) {
    init_board();
    int is_white = 1;
    int limit = (index < move_count) ? index : move_count;
    for (int i = 0; i < limit; i++) {
        Move m = {0};
        if (!parse_san(moves[i], is_white, &m)) {
            printf("Failed to parse move: %s\n", moves[i]);
            break;
        }
        apply_move(&m, is_white);
        is_white = !is_white;
    }
    draw_board();
}

int play_game(const char *move_buffer) {
    char moves[MAX_MOVES][MOVE_TEXT_LEN];
    int move_count = build_move_list(move_buffer, moves, MAX_MOVES);

    init_board();
    draw_board();

    int index = 0;
    int paused = 0;
    int quit = 0;
    Uint32 last_move_tick = SDL_GetTicks();

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = 1;
            } else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    quit = 1;
                } else if (key == SDLK_SPACE) {
                    paused = !paused;
                    last_move_tick = SDL_GetTicks();
                } else if (paused && key == SDLK_LEFT) {
                    if (index > 0) {
                        index--;
                        replay_moves_to_index(moves, move_count, index);
                    }
                } else if (paused && key == SDLK_RIGHT) {
                    if (index < move_count) {
                        index++;
                        replay_moves_to_index(moves, move_count, index);
                    }
                }
            }
        }
        if (quit) break;

        if (!paused && index < move_count) {
            Uint32 now = SDL_GetTicks();
            if (now - last_move_tick >= MOVE_DELAY_MS) {
                int is_white = (index % 2 == 0);
                Move m = {0};
                if (parse_san(moves[index], is_white, &m)) {
                    apply_move(&m, is_white);
                    draw_board();
                } else {
                    printf("Failed to parse move: %s\n", moves[index]);
                }
                index++;
                last_move_tick = now;
            }
        } else if (index >= move_count) {
            break;
        }

        SDL_Delay(10);
    }

    Uint32 end_tick = SDL_GetTicks();
    while (!quit && SDL_GetTicks() - end_tick < 2000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                quit = 1;
            }
        }
        SDL_Delay(10);
    }
    return quit;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <pgn_file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0 || !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("SDL init error: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Chess PGN Screensaver", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCREEN_SIZE, SCREEN_SIZE, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        printf("SDL window/renderer error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    char line[512];
    char move_buffer[65536] = {0};
    int in_game = 0;

    int quit = 0;
    while (!quit && fgets(line, sizeof(line), fp)) {
        clean_line(line);
        if (strstr(line, "[Event")) {  // New game starts
            if (in_game && strlen(move_buffer) > 0) {
                if (play_game(move_buffer)) {
                    quit = 1;
                    break;
                }
                move_buffer[0] = '\0';  // Reset buffer
            }
            in_game = 1;
            continue;  // Skip header lines
        }
        if (in_game && strlen(line) > 0 && line[0] != '[') {
            strncat(move_buffer, " ", sizeof(move_buffer) - strlen(move_buffer) - 1);  // Add space separator
            strncat(move_buffer, line, sizeof(move_buffer) - strlen(move_buffer) - 1);
        }
    }
    // Play the last game
    if (!quit && in_game && strlen(move_buffer) > 0) {
        quit = play_game(move_buffer);
    }

    fclose(fp);

    // Cleanup
    for (int i = 0; i < 256; i++) {
        if (piece_textures[i]) SDL_DestroyTexture(piece_textures[i]);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();

    return 0;
}
