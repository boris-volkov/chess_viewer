// chess_viewer.c - A minimal PGN chess viewer using SDL2 for graphical display
// Displays games as an animated playback with per-move delays
// Dependencies: SDL2 and SDL2_image (for loading PNG piece images)
// Compile (Linux/Mac): gcc chess_viewer.c -o chess_viewer -lSDL2 -lSDL2_image
// Windows: Use a setup like MinGW or Visual Studio with SDL2 libs

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define BOARD_SIZE 8
#define SCREEN_SIZE 800
#define DEFAULT_GAMES_DIR "games"
#define MAX_MOVES 8192
#define MOVE_TEXT_LEN 32
#define MOVE_DELAY_MS 5000
#define MOVE_ANIM_MS 300
#define NAME_LEN 128
#define YEAR_LEN 5
#define RESULT_LEN 16
#define GAME_OVER_PAUSE_MS 10000
#define KING_FLIP_MS 800

#ifdef _WIN32
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

char board[BOARD_SIZE][BOARD_SIZE];
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *piece_textures[256] = {NULL};
char current_white_name[NAME_LEN] = "White";
char current_black_name[NAME_LEN] = "Black";
char current_game_year[YEAR_LEN] = "";
int show_loser_king = 0;
int loser_is_white = 0;
float loser_king_angle = 180.0f;
int show_draw_kings = 0;
float draw_king_angle = 90.0f;
int view_from_white = 1;
int dim_board = 0;
int pause_buffered = 0;

typedef struct {
    int square;
    int offset_x;
    int offset_y;
    int screen_w;
    int screen_h;
    int board_px;
} BoardView;

typedef struct {
    int active;
    char piece;
    float x;
    float y;
    int skip_r1, skip_f1;
} Overlay;

int is_in_check(int is_white);
void board_to_screen(const BoardView *view, int board_r, int board_f, int *out_x, int *out_y);

static int is_white_piece(char piece) {
    return (piece >= 'A' && piece <= 'Z');
}

int find_king_pos(char king, int *out_r, int *out_f) {
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int f = 0; f < BOARD_SIZE; f++) {
            if (board[r][f] == king) {
                *out_r = r;
                *out_f = f;
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    char c;
    unsigned char rows[7];
} Glyph;

static const Glyph font_glyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}},
    {'\'',{0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}},
    {':', {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}
};

static const unsigned char *get_glyph_rows(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (size_t i = 0; i < sizeof(font_glyphs) / sizeof(font_glyphs[0]); i++) {
        if (font_glyphs[i].c == c) return font_glyphs[i].rows;
    }
    return font_glyphs[7].rows;  // '?'
}

int text_width_px(const char *text, int scale) {
    int len = (int)strlen(text);
    if (len <= 0) return 0;
    return (len * 6 - 1) * scale;
}

void draw_text(int x, int y, int scale, const char *text, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int pen_x = x;
    for (const char *p = text; *p; p++) {
        const unsigned char *rows = get_glyph_rows(*p);
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 5; c++) {
                if (rows[r] & (1 << (4 - c))) {
                    SDL_Rect rect = {pen_x + c * scale, y + r * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }
        pen_x += 6 * scale;
    }
}

void draw_color_swatch(int x, int y, int size, SDL_Color fill, SDL_Color outline) {
    SDL_Rect rect = {x, y, size, size};
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void render_year_label(const BoardView *view) {
    if (current_game_year[0] == '\0') return;

    int scale = (view->square >= 60) ? 3 : 2;
    int margin = (view->square >= 60) ? 16 : 8;
    int text_w = text_width_px(current_game_year, scale);
    int text_h = 7 * scale;

    int x = view->offset_x + margin;
    int y = view->offset_y + margin;
    if (view->offset_y >= text_h + 2 * margin) {
        y = view->offset_y - margin - text_h;
        x = view->offset_x + margin;
    } else if (view->offset_x >= text_w + 2 * margin) {
        x = view->offset_x - margin - text_w;
        y = view->offset_y + margin;
    }

    SDL_Color text_color = {255, 255, 255, 255};
    draw_text(x, y, scale, current_game_year, text_color);
}

void render_player_labels(const BoardView *view) {
    int margin = (view->square >= 60) ? 16 : 8;
    int right_x0 = view->offset_x + view->board_px + margin;
    int right_x1 = view->screen_w - margin;
    if (right_x1 <= right_x0) return;

    int swatch_size = (view->square >= 60) ? 16 : 12;
    int gap = 6;
    int avail_text_w = right_x1 - right_x0 - swatch_size - gap;
    if (avail_text_w <= 0) return;

    const char *white_name = (current_white_name[0] != '\0') ? current_white_name : "White";
    const char *black_name = (current_black_name[0] != '\0') ? current_black_name : "Black";
    int top_is_white = view_from_white ? 0 : 1;
    const char *top_name = top_is_white ? white_name : black_name;
    const char *bottom_name = top_is_white ? black_name : white_name;
    int max_len = (int)strlen(white_name);
    int black_len = (int)strlen(black_name);
    if (black_len > max_len) max_len = black_len;

    int scale = 3;
    int need_w = (max_len > 0) ? text_width_px(white_name, scale) : 0;
    if (black_len > 0) {
        int black_w = text_width_px(black_name, scale);
        if (black_w > need_w) need_w = black_w;
    }
    if (need_w > avail_text_w && max_len > 0) {
        int denom = max_len * 6 - 1;
        scale = avail_text_w / denom;
        if (scale < 1) scale = 1;
    }

    int text_h = 7 * scale;
    if (swatch_size > text_h) swatch_size = text_h;
    int top_y = view->offset_y + margin;
    int bottom_y = view->offset_y + view->board_px - margin - text_h;
    if (bottom_y < top_y) bottom_y = top_y;

    SDL_Color text_color = {230, 230, 230, 255};
    SDL_Color black_fill = {20, 20, 20, 255};
    SDL_Color white_fill = {230, 230, 230, 255};
    SDL_Color outline = {30, 30, 30, 255};

    int swatch_y_top = top_y + (text_h - swatch_size) / 2;
    int swatch_y_bottom = bottom_y + (text_h - swatch_size) / 2;

    if (top_is_white) {
        draw_color_swatch(right_x0, swatch_y_top, swatch_size, white_fill, outline);
    } else {
        draw_color_swatch(right_x0, swatch_y_top, swatch_size, black_fill, white_fill);
    }
    draw_text(right_x0 + swatch_size + gap, top_y, scale, top_name, text_color);

    if (top_is_white) {
        draw_color_swatch(right_x0, swatch_y_bottom, swatch_size, black_fill, white_fill);
    } else {
        draw_color_swatch(right_x0, swatch_y_bottom, swatch_size, white_fill, outline);
    }
    draw_text(right_x0 + swatch_size + gap, bottom_y, scale, bottom_name, text_color);
}

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
    snprintf(path, sizeof(path), "pieces/Chess_%c%s.png", letter, color);

    SDL_Texture *tex = IMG_LoadTexture(renderer, path);
    if (!tex) {
        printf("Failed to load %s: %s\n", path, IMG_GetError());
    }
    piece_textures[idx] = tex;
    return tex;
}

void render_rotated_king(const BoardView *view, char king, float angle) {
    int r = -1, f = -1;
    if (!find_king_pos(king, &r, &f)) return;
    SDL_Texture *tex = get_piece_texture(king);
    if (!tex) return;
    int x = 0;
    int y = 0;
    board_to_screen(view, r, f, &x, &y);
    SDL_Rect rect = {x, y, view->square, view->square};
    SDL_RenderCopyEx(renderer, tex, NULL, &rect, angle, NULL, SDL_FLIP_NONE);
}

void board_to_screen(const BoardView *view, int board_r, int board_f, int *out_x, int *out_y) {
    int draw_r = view_from_white ? board_r : (BOARD_SIZE - 1 - board_r);
    int draw_f = view_from_white ? board_f : (BOARD_SIZE - 1 - board_f);
    if (out_x) *out_x = view->offset_x + draw_f * view->square;
    if (out_y) *out_y = view->offset_y + draw_r * view->square;
}

void get_board_view(BoardView *view) {
    int screen_w = SCREEN_SIZE;
    int screen_h = SCREEN_SIZE;
    if (SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h) != 0) {
        screen_w = SCREEN_SIZE;
        screen_h = SCREEN_SIZE;
    }
    int min_dim = (screen_w < screen_h) ? screen_w : screen_h;
    view->square = min_dim / BOARD_SIZE;
    if (view->square < 1) view->square = 1;
    view->board_px = view->square * BOARD_SIZE;
    view->offset_x = (screen_w - view->board_px) / 2;
    view->offset_y = (screen_h - view->board_px) / 2;
    view->screen_w = screen_w;
    view->screen_h = screen_h;
}

void render_board(const BoardView *view, const Overlay *overlay) {
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderClear(renderer);

    SDL_Color light = {210, 210, 210, 255};
    SDL_Color dark  = {150,  150,  150, 255};
    if (dim_board) {
        light.r = (Uint8)(light.r * 2 / 3);
        light.g = (Uint8)(light.g * 2 / 3);
        light.b = (Uint8)(light.b * 2 / 3);
        dark.r = (Uint8)(dark.r * 2 / 3);
        dark.g = (Uint8)(dark.g * 2 / 3);
        dark.b = (Uint8)(dark.b * 2 / 3);
    }

    for (int row = 0; row < BOARD_SIZE; row++) {  // row 0 = rank 8
        for (int col = 0; col < BOARD_SIZE; col++) {
            SDL_Color colr = ((row + col) % 2 == 0) ? light : dark;
            SDL_SetRenderDrawColor(renderer, colr.r, colr.g, colr.b, colr.a);
            int x = 0;
            int y = 0;
            board_to_screen(view, row, col, &x, &y);
            SDL_Rect rect = {x, y, view->square, view->square};
            SDL_RenderFillRect(renderer, &rect);

            int skip = 0;
            if (overlay && overlay->active) {
                if (row == overlay->skip_r1 && col == overlay->skip_f1) {
                    skip = 1;
                }
            }
            if (!skip && show_loser_king) {
                char losing_piece = loser_is_white ? 'K' : 'k';
                if (board[row][col] == losing_piece) {
                    skip = 1;
                }
            }
            if (!skip && show_draw_kings) {
                if (board[row][col] == 'K' || board[row][col] == 'k') {
                    skip = 1;
                }
            }
            char piece = skip ? '.' : board[row][col];
            SDL_Texture *tex = get_piece_texture(piece);
            if (tex) {
                SDL_RenderCopy(renderer, tex, NULL, &rect);
            }
        }
    }

    if (overlay && overlay->active) {
        SDL_Texture *tex = get_piece_texture(overlay->piece);
        if (tex) {
            SDL_Rect rect = {(int)(overlay->x + 0.5f), (int)(overlay->y + 0.5f),
                             view->square, view->square};
            SDL_RenderCopy(renderer, tex, NULL, &rect);
        }
    }

    if (show_draw_kings) {
        render_rotated_king(view, 'K', draw_king_angle);
        render_rotated_king(view, 'k', draw_king_angle);
    } else if (show_loser_king) {
        char losing_piece = loser_is_white ? 'K' : 'k';
        render_rotated_king(view, losing_piece, loser_king_angle);
    }

    int thickness = (view->square >= 60) ? 4 : 2;
    SDL_SetRenderDrawColor(renderer, 200, 20, 20, 255);
    if (is_in_check(1)) {
        int r = -1, f = -1;
        if (find_king_pos('K', &r, &f)) {
            int x = 0;
            int y = 0;
            board_to_screen(view, r, f, &x, &y);
            SDL_Rect rect = {x, y, view->square, view->square};
            for (int i = 0; i < thickness; i++) {
                SDL_Rect r2 = {rect.x + i, rect.y + i, rect.w - 2 * i, rect.h - 2 * i};
                if (r2.w <= 0 || r2.h <= 0) break;
                SDL_RenderDrawRect(renderer, &r2);
            }
        }
    }
    if (is_in_check(0)) {
        int r = -1, f = -1;
        if (find_king_pos('k', &r, &f)) {
            int x = 0;
            int y = 0;
            board_to_screen(view, r, f, &x, &y);
            SDL_Rect rect = {x, y, view->square, view->square};
            for (int i = 0; i < thickness; i++) {
                SDL_Rect r2 = {rect.x + i, rect.y + i, rect.w - 2 * i, rect.h - 2 * i};
                if (r2.w <= 0 || r2.h <= 0) break;
                SDL_RenderDrawRect(renderer, &r2);
            }
        }
    }

    render_year_label(view);
    render_player_labels(view);

    SDL_RenderPresent(renderer);
}

void draw_board() {
    BoardView view;
    get_board_view(&view);
    render_board(&view, NULL);
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
    int is_enemy = !is_empty && (is_white_piece(at_to) != is_white);

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
            if (p == '.' || (is_white_piece(p) != opponent_is_white)) continue;  // Not opponent piece
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

int animate_move(const Move *m, int is_white) {
    char piece = board[m->from_r][m->from_f];
    if (piece == '.') return 0;

    BoardView view;
    get_board_view(&view);

    int start_x = 0;
    int start_y = 0;
    int end_x = 0;
    int end_y = 0;
    board_to_screen(&view, m->from_r, m->from_f, &start_x, &start_y);
    board_to_screen(&view, m->to_r, m->to_f, &end_x, &end_y);
    Uint32 start = SDL_GetTicks();

    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                return 1;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE) {
                pause_buffered = 1;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_f) {
                view_from_white = !view_from_white;
                get_board_view(&view);
                board_to_screen(&view, m->from_r, m->from_f, &start_x, &start_y);
                board_to_screen(&view, m->to_r, m->to_f, &end_x, &end_y);
            }
        }

        Uint32 now = SDL_GetTicks();
        float t = (MOVE_ANIM_MS > 0) ? (float)(now - start) / (float)MOVE_ANIM_MS : 1.0f;
        if (t > 1.0f) t = 1.0f;

        Overlay overlay;
        overlay.active = 1;
        overlay.piece = piece;
        overlay.x = start_x + (end_x - start_x) * t;
        overlay.y = start_y + (end_y - start_y) * t;
        overlay.skip_r1 = m->from_r;
        overlay.skip_f1 = m->from_f;
        render_board(&view, &overlay);

        if (t >= 1.0f) break;
        SDL_Delay(10);
    }
    return 0;
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

int build_move_list(const char *move_buffer, char moves[][MOVE_TEXT_LEN], int max_moves,
                    char *result_out, size_t result_out_size) {
    char temp_buffer[65536];
    strncpy(temp_buffer, move_buffer, sizeof(temp_buffer) - 1);
    temp_buffer[sizeof(temp_buffer) - 1] = '\0';

    if (result_out && result_out_size > 0) {
        result_out[0] = '\0';
    }

    int count = 0;
    char *token = strtok(temp_buffer, " \t\n\r");
    while (token) {
        char san_buf[MOVE_TEXT_LEN];
        if (extract_san_token(token, san_buf, sizeof(san_buf))) {
            if (is_result_token(san_buf)) {
                if (result_out && result_out_size > 0) {
                    strncpy(result_out, san_buf, result_out_size - 1);
                    result_out[result_out_size - 1] = '\0';
                }
                break;
            }
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

int loser_from_result(const char *result, int *out_loser_is_white) {
    if (!result) return 0;
    if (strcmp(result, "1-0") == 0) {
        if (out_loser_is_white) *out_loser_is_white = 0;
        return 1;
    }
    if (strcmp(result, "0-1") == 0) {
        if (out_loser_is_white) *out_loser_is_white = 1;
        return 1;
    }
    return 0;
}

int is_draw_result(const char *result) {
    return (result && strcmp(result, "1/2-1/2") == 0);
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

typedef struct {
    char *moves;
    char white[NAME_LEN];
    char black[NAME_LEN];
    char year[YEAR_LEN];
    char result[RESULT_LEN];
} Game;

char *copy_string(const char *s) {
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

int push_string(char ***items, int *count, int *cap, const char *value) {
    if (*count >= *cap) {
        int new_cap = (*cap == 0) ? 16 : (*cap * 2);
        char **new_items = (char **)realloc(*items, (size_t)new_cap * sizeof(char *));
        if (!new_items) return 0;
        *items = new_items;
        *cap = new_cap;
    }
    (*items)[*count] = copy_string(value);
    if (!(*items)[*count]) return 0;
    (*count)++;
    return 1;
}

void free_string_list(char **items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

int has_pgn_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    if (dot[1] == '\0' || dot[2] == '\0' || dot[3] == '\0' || dot[4] != '\0') return 0;
    return (tolower((unsigned char)dot[1]) == 'p' &&
            tolower((unsigned char)dot[2]) == 'g' &&
            tolower((unsigned char)dot[3]) == 'n');
}

char *join_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    int need_sep = (dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\');
    size_t len = dir_len + (need_sep ? 1 : 0) + strlen(name) + 1;
    char *out = (char *)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s%s%s", dir, need_sep ? PATH_SEP_STR : "", name);
    return out;
}

int list_pgn_files(const char *dir, char ***out_files) {
    char **files = NULL;
    int count = 0;
    int cap = 0;

#ifdef _WIN32
    char *search = join_path(dir, "*");
    if (!search) return -1;
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(search, &data);
    free(search);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!has_pgn_extension(data.cFileName)) continue;
        if (!push_string(&files, &count, &cap, data.cFileName)) {
            FindClose(h);
            free_string_list(files, count);
            return -1;
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
#ifdef DT_DIR
        if (ent->d_type == DT_DIR) continue;
#endif
        if (!has_pgn_extension(ent->d_name)) continue;
        if (!push_string(&files, &count, &cap, ent->d_name)) {
            closedir(d);
            free_string_list(files, count);
            return -1;
        }
    }
    closedir(d);
#endif

    *out_files = files;
    return count;
}

int parse_tag_value(const char *line, const char *tag, char *out, size_t out_size) {
    size_t tag_len = strlen(tag);
    if (strncmp(line, "[", 1) != 0) return 0;
    if (strncmp(line + 1, tag, tag_len) != 0) return 0;
    if (!isspace((unsigned char)line[1 + tag_len])) return 0;
    const char *first_quote = strchr(line, '"');
    if (!first_quote) return 0;
    const char *second_quote = strchr(first_quote + 1, '"');
    if (!second_quote) return 0;
    size_t len = (size_t)(second_quote - first_quote - 1);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, first_quote + 1, len);
    out[len] = '\0';
    return 1;
}

void extract_year(char *out, size_t out_size, const char *date) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!date || strlen(date) < 4) return;
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)date[i])) return;
    }
    if (out_size < 5) return;
    memcpy(out, date, 4);
    out[4] = '\0';
}

void set_last_name(char *out, size_t out_size, const char *full) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!full) return;

    const char *start = full;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0') return;

    const char *comma = strchr(start, ',');
    const char *name_start = start;
    const char *name_end = NULL;

    if (comma) {
        name_end = comma;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) {
            name_end--;
        }
    } else {
        const char *p = start;
        const char *last_word = start;
        while (*p) {
            while (isspace((unsigned char)*p)) p++;
            if (*p == '\0') break;
            last_word = p;
            while (*p && !isspace((unsigned char)*p)) p++;
        }
        name_start = last_word;
        name_end = name_start;
        while (*name_end && !isspace((unsigned char)*name_end)) name_end++;
    }

    size_t len = (size_t)(name_end - name_start);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, name_start, len);
    out[len] = '\0';
}

int push_game(Game **games, int *count, int *cap, const char *move_buffer,
              const char *white, const char *black, const char *year, const char *result) {
    if (*count >= *cap) {
        int new_cap = (*cap == 0) ? 16 : (*cap * 2);
        Game *new_games = (Game *)realloc(*games, (size_t)new_cap * sizeof(Game));
        if (!new_games) return 0;
        *games = new_games;
        *cap = new_cap;
    }
    (*games)[*count].moves = copy_string(move_buffer);
    if (!(*games)[*count].moves) return 0;
    strncpy((*games)[*count].white, (white && white[0]) ? white : "White", NAME_LEN - 1);
    (*games)[*count].white[NAME_LEN - 1] = '\0';
    strncpy((*games)[*count].black, (black && black[0]) ? black : "Black", NAME_LEN - 1);
    (*games)[*count].black[NAME_LEN - 1] = '\0';
    strncpy((*games)[*count].year, (year && year[0]) ? year : "", YEAR_LEN - 1);
    (*games)[*count].year[YEAR_LEN - 1] = '\0';
    strncpy((*games)[*count].result, (result && result[0]) ? result : "", RESULT_LEN - 1);
    (*games)[*count].result[RESULT_LEN - 1] = '\0';
    (*count)++;
    return 1;
}

void free_games(Game *games, int count) {
    if (!games) return;
    for (int i = 0; i < count; i++) {
        free(games[i].moves);
    }
    free(games);
}

int load_games(FILE *fp, Game **out_games) {
    Game *games = NULL;
    int count = 0;
    int cap = 0;
    char line[512];
    char move_buffer[65536] = {0};
    char current_white[NAME_LEN] = "";
    char current_black[NAME_LEN] = "";
    char current_date[NAME_LEN] = "";
    char current_year[YEAR_LEN] = "";
    char current_result[RESULT_LEN] = "";
    int in_game = 0;

    while (fgets(line, sizeof(line), fp)) {
        clean_line(line);
        const char *trim = line;
        while (isspace((unsigned char)*trim)) trim++;
        if (strncmp(trim, "[Event", 6) == 0) {  // New game starts
            if (in_game && move_buffer[0] != '\0') {
                if (!push_game(&games, &count, &cap, move_buffer,
                               current_white, current_black, current_year, current_result)) goto error;
                move_buffer[0] = '\0';
            }
            current_white[0] = '\0';
            current_black[0] = '\0';
            current_date[0] = '\0';
            current_year[0] = '\0';
            current_result[0] = '\0';
            in_game = 1;
            continue;  // Skip header lines
        }
        if (in_game && trim[0] == '[') {
            parse_tag_value(trim, "White", current_white, sizeof(current_white));
            parse_tag_value(trim, "Black", current_black, sizeof(current_black));
            if (parse_tag_value(trim, "Date", current_date, sizeof(current_date))) {
                extract_year(current_year, sizeof(current_year), current_date);
            }
            parse_tag_value(trim, "Result", current_result, sizeof(current_result));
            continue;
        }
        if (in_game && trim[0] != '[' && trim[0] != '\0') {
            strncat(move_buffer, " ", sizeof(move_buffer) - strlen(move_buffer) - 1);
            strncat(move_buffer, trim, sizeof(move_buffer) - strlen(move_buffer) - 1);
        }
    }

    if (in_game && move_buffer[0] != '\0') {
        if (!push_game(&games, &count, &cap, move_buffer,
                       current_white, current_black, current_year, current_result)) goto error;
    }

    *out_games = games;
    return count;

error:
    free_games(games, count);
    *out_games = NULL;
    return -1;
}

void shuffle_games(Game *games, int count) {
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Game tmp = games[i];
        games[i] = games[j];
        games[j] = tmp;
    }
}

int play_game(const char *move_buffer, const char *header_result) {
    char moves[MAX_MOVES][MOVE_TEXT_LEN];
    char result_buf[RESULT_LEN];
    int move_count = build_move_list(move_buffer, moves, MAX_MOVES, result_buf, sizeof(result_buf));
    const char *result = (result_buf[0] != '\0') ? result_buf : header_result;
    int has_loser = 0;
    int loser_is_white_local = 0;
    int is_draw = 0;
    if (result && result[0] != '\0') {
        has_loser = loser_from_result(result, &loser_is_white_local);
        is_draw = is_draw_result(result);
    }

    init_board();
    draw_board();

    int index = 0;
    int paused = 0;
    int quit = 0;
    Uint32 last_move_tick = SDL_GetTicks();
    show_loser_king = 0;
    show_draw_kings = 0;
    dim_board = 0;
    pause_buffered = 0;

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
                    dim_board = paused;
                    last_move_tick = SDL_GetTicks();
                    draw_board();
                } else if (key == SDLK_f) {
                    view_from_white = !view_from_white;
                    draw_board();
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
                    if (animate_move(&m, is_white)) {
                        quit = 1;
                        break;
                    }
                    if (pause_buffered) {
                        paused = 1;
                        dim_board = 1;
                        pause_buffered = 0;
                        last_move_tick = SDL_GetTicks();
                    }
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

    int pause_ms = 2000;
    Uint32 pause_start = SDL_GetTicks();
    dim_board = 0;
    if (!quit && index >= move_count) {
        pause_ms = GAME_OVER_PAUSE_MS;
        if (has_loser) {
            show_loser_king = 1;
            loser_is_white = loser_is_white_local;
            loser_king_angle = 0.0f;
            Uint32 flip_start = SDL_GetTicks();
            for (;;) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) {
                        quit = 1;
                    } else if (e.type == SDL_KEYDOWN) {
                        SDL_Keycode key = e.key.keysym.sym;
                        if (key == SDLK_ESCAPE) {
                            quit = 1;
                        } else if (key == SDLK_f) {
                            view_from_white = !view_from_white;
                            draw_board();
                        }
                    }
                }
                if (quit) break;

                Uint32 now = SDL_GetTicks();
                float t = (KING_FLIP_MS > 0) ? (float)(now - flip_start) / (float)KING_FLIP_MS : 1.0f;
                if (t > 1.0f) t = 1.0f;
                loser_king_angle = 180.0f * t;
                draw_board();
                if (t >= 1.0f) break;
                SDL_Delay(10);
            }
        } else if (is_draw) {
            show_draw_kings = 1;
            draw_king_angle = 0.0f;
            Uint32 tilt_start = SDL_GetTicks();
            for (;;) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) {
                        quit = 1;
                    } else if (e.type == SDL_KEYDOWN) {
                        SDL_Keycode key = e.key.keysym.sym;
                        if (key == SDLK_ESCAPE) {
                            quit = 1;
                        } else if (key == SDLK_f) {
                            view_from_white = !view_from_white;
                            draw_board();
                        }
                    }
                }
                if (quit) break;

                Uint32 now = SDL_GetTicks();
                float t = (KING_FLIP_MS > 0) ? (float)(now - tilt_start) / (float)KING_FLIP_MS : 1.0f;
                if (t > 1.0f) t = 1.0f;
                draw_king_angle = 90.0f * t;
                draw_board();
                if (t >= 1.0f) break;
                SDL_Delay(10);
            }
        }
    }

    int pause_hold = 0;
    Uint32 pause_hold_start = 0;
    Uint32 pause_hold_total = 0;
    int review_index = index;
    while (!quit) {
        Uint32 now = SDL_GetTicks();
        if (!pause_hold && now - pause_start - pause_hold_total >= (Uint32)pause_ms) {
            break;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                quit = 1;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE) {
                if (!pause_hold) {
                    pause_hold = 1;
                    pause_hold_start = now;
                    dim_board = 1;
                    draw_board();
                } else {
                    pause_hold = 0;
                    pause_hold_total += now - pause_hold_start;
                    dim_board = 0;
                    draw_board();
                }
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_f) {
                view_from_white = !view_from_white;
                draw_board();
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_LEFT) {
                if (review_index > 0) {
                    review_index--;
                    show_loser_king = 0;
                    show_draw_kings = 0;
                    replay_moves_to_index(moves, move_count, review_index);
                }
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RIGHT) {
                if (review_index < move_count) {
                    review_index++;
                    show_loser_king = 0;
                    show_draw_kings = 0;
                    replay_moves_to_index(moves, move_count, review_index);
                }
            }
        }
        if (!quit && review_index == move_count) {
            if (has_loser) {
                show_loser_king = 1;
                show_draw_kings = 0;
                draw_board();
            } else if (is_draw) {
                show_draw_kings = 1;
                show_loser_king = 0;
                draw_board();
            }
        }
        SDL_Delay(10);
    }
    show_loser_king = 0;
    show_draw_kings = 0;
    dim_board = 0;
    return quit;
}

int main(int argc, char *argv[]) {
    const char *games_dir = DEFAULT_GAMES_DIR;
    (void)argc;
    (void)argv;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0 || !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("SDL init error: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Chess Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCREEN_SIZE, SCREEN_SIZE, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        printf("SDL window/renderer error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);

    srand((unsigned int)time(NULL));

    int quit = 0;
    while (!quit) {
        char **files = NULL;
        int file_count = list_pgn_files(games_dir, &files);
        if (file_count <= 0) {
            if (file_count < 0) {
                printf("Failed to read PGN directory %s\n", games_dir);
            } else {
                printf("No PGN files found in %s\n", games_dir);
            }
            free_string_list(files, file_count);
            break;
        }

        int file_index = rand() % file_count;
        char *path = join_path(games_dir, files[file_index]);
        free_string_list(files, file_count);
        if (!path) {
            printf("Failed to build PGN path.\n");
            break;
        }

        FILE *fp = fopen(path, "r");
        if (!fp) {
            printf("Failed to open %s\n", path);
            free(path);
            SDL_Delay(500);
            continue;
        }

        Game *games = NULL;
        int game_count = load_games(fp, &games);
        fclose(fp);
        free(path);
        if (game_count <= 0) {
            if (game_count < 0) {
                printf("Failed to load games from PGN.\n");
            }
            free_games(games, game_count);
            SDL_Delay(500);
            continue;
        }

        int game_index = rand() % game_count;
        set_last_name(current_white_name, sizeof(current_white_name), games[game_index].white);
        if (current_white_name[0] == '\0') {
            strncpy(current_white_name, games[game_index].white, NAME_LEN - 1);
            current_white_name[NAME_LEN - 1] = '\0';
        }
        set_last_name(current_black_name, sizeof(current_black_name), games[game_index].black);
        if (current_black_name[0] == '\0') {
            strncpy(current_black_name, games[game_index].black, NAME_LEN - 1);
            current_black_name[NAME_LEN - 1] = '\0';
        }
        strncpy(current_game_year, games[game_index].year, YEAR_LEN - 1);
        current_game_year[YEAR_LEN - 1] = '\0';
        view_from_white = (rand() % 2) ? 1 : 0;
        quit = play_game(games[game_index].moves, games[game_index].result);
        free_games(games, game_count);
    }

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
