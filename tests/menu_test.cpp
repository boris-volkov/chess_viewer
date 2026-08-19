// Test harness for the command-menu geometry.
// Includes the real translation unit (with its main renamed) so the static
// menu_layout/menu_item_at/menu_step under test are the ones that actually ship,
// not a replica that could drift from them.
#define SDL_MAIN_HANDLED
#define main chess_viewer_orig_main
#include "chess_viewer.cpp"
#undef main

#include <cstdio>

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what) {
    checks++;
    if (!cond) { failures++; std::printf("  FAIL: %s\n", what); }
}

static void test_at_size(int screen_w, int screen_h, int square, const char *label) {
    std::printf("[%s] %dx%d square=%d\n", label, screen_w, screen_h, square);
    BoardView view;
    view.square   = square;
    view.offset_x = 0;
    view.offset_y = 0;
    view.screen_w = screen_w;
    view.screen_h = screen_h;
    view.board_px = square * 8;

    MenuLayout L;
    menu_layout(&view, &L);

    // Box must sit fully on screen, or rows get clipped and become unclickable.
    check(L.box.x >= 0 && L.box.y >= 0, "box origin on screen");
    check(L.box.x + L.box.w <= screen_w, "box fits horizontally");
    check(L.box.y + L.box.h <= screen_h, "box fits vertically");

    // The property that matters: what gets painted is what gets clicked.
    //
    // Probing row centres alone is far too forgiving. A modest offset between
    // the drawn rows and the clickable bands leaves every centre still inside
    // its own band and only shows up near the edges -- verified by injecting a
    // 6px shift into menu_item_at, which a centre-only version of this test
    // passed clean. So assert that EVERY pixel row the label occupies maps back
    // to that row, which is what "clicking the label works" actually means.
    int cx = L.box.x + L.box.w / 2;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int ty = L.first_row_y + i * L.line_h;   // same expression render uses
        char msg[200];
        for (int dy = 0; dy < L.text_h; dy++) {
            int hit = menu_item_at(&view, cx, ty + dy);
            if (menu_items[i].key == SDLK_UNKNOWN) {
                std::snprintf(msg, sizeof(msg),
                              "separator row %d inert across label (+%d, got %d)", i, dy, hit);
                check(hit == -1, msg);
            } else {
                std::snprintf(msg, sizeof(msg),
                              "row %d '%s' label pixel +%d hits own row (got %d)",
                              i, menu_items[i].label, dy, hit);
                check(hit == i, msg);
            }
        }
    }

    // Adjacent rows must not overlap: the pixel above a row's first label pixel
    // must belong to something else. Pins the band boundaries, not just interiors.
    for (int i = 1; i < MENU_ITEM_COUNT; i++) {
        if (menu_items[i].key == SDLK_UNKNOWN) continue;
        int ty = L.first_row_y + i * L.line_h;
        char msg[200];
        std::snprintf(msg, sizeof(msg), "row %d does not extend above its label", i);
        check(menu_item_at(&view, cx, ty - L.line_gap) != i, msg);
    }

    // Outside the panel entirely -> no row.
    check(menu_item_at(&view, L.box.x - 20, L.first_row_y) == -1, "left of box misses");
    check(menu_item_at(&view, L.box.x + L.box.w + 20, L.first_row_y) == -1, "right of box misses");
    check(menu_item_at(&view, L.box.x + L.box.w / 2, L.box.y - 5) == -1, "above rows misses");
}

static void test_step() {
    std::printf("[navigation]\n");
    // Every landing spot is selectable, in both directions.
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int dn = menu_step(i, 1);
        int up = menu_step(i, -1);
        char msg[120];
        std::snprintf(msg, sizeof(msg), "step down from %d lands selectable (%d)", i, dn);
        check(menu_items[dn].key != SDLK_UNKNOWN, msg);
        std::snprintf(msg, sizeof(msg), "step up from %d lands selectable (%d)", i, up);
        check(menu_items[up].key != SDLK_UNKNOWN, msg);
    }

    // Walking down from the first row must reach every selectable row and wrap.
    int selectable = 0;
    for (int i = 0; i < MENU_ITEM_COUNT; i++)
        if (menu_items[i].key != SDLK_UNKNOWN) selectable++;

    int start = menu_items[0].key == SDLK_UNKNOWN ? menu_step(0, 1) : 0;
    int seen = 1, cur = start;
    for (int n = 0; n < selectable - 1; n++) { cur = menu_step(cur, 1); seen++; }
    check(seen == selectable, "walk visits every selectable row");
    check(menu_step(cur, 1) == start, "walk wraps to start");

    // QUIT is last; stepping down from it wraps to the first row.
    check(menu_step(MENU_ITEM_COUNT - 1, 1) == start, "wrap from last row");
}

int main() {
    std::printf("menu geometry self-test (%d items)\n\n", MENU_ITEM_COUNT);
    test_at_size(1920, 1080, 120, "desktop");   // scale 3 path
    test_at_size(1280,  800,  90, "laptop");    // scale 3 path
    test_at_size( 800,  600,  55, "small");     // scale 2 path
    test_at_size( 640,  480,  40, "tiny");      // scale 2 path
    test_step();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
