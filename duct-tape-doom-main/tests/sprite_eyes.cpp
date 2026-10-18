// Data-free checks for glowing eyes: detection (platform/sprite_eyes.h)
// and the authored stock table (platform/sprite_eyes_stock.h).
#include "../platform/sprite_eyes.h"
#include "../platform/sprite_eyes_stock.h"
#include <cstdio>
#include <cstdlib>

static void check(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
int main()
{
    // 1: skin, 2: skin in shadow, 3: pure red.
    unsigned char palette[768] = {0};
    const unsigned char colors[4][3] = {{0, 0, 0}, {200, 140, 100}, {150, 100, 70}, {230, 0, 0}};
    for (int i = 0; i < 4; ++i) for (int c = 0; c < 3; ++c) palette[i * 3 + c] = colors[i][c];
    enum { W = 30, H = 60 };
    static unsigned char pixels[W * H * 2];
    auto paint = [&](int x, int y, int index) { pixels[(y * W + x) * 2] = (unsigned char)index; pixels[(y * W + x) * 2 + 1] = 255; };
    // An upright figure in skin with a shaded side, two red eyes and a red
    // mouth below them.
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) paint(x, y, x < 20 ? 1 : 2);
    paint(10, 8, 3); paint(11, 8, 3); paint(17, 8, 3); paint(18, 8, 3);
    paint(13, 13, 3); paint(14, 13, 3); paint(15, 13, 3);
    std::vector<unsigned char> eyes = doom_sprite_eyes(W, H, pixels, palette);
    check(eyes[8 * W + 10] == 255 && eyes[8 * W + 18] == 255, "Both eyes must glow");
    check(eyes[13 * W + 14] == 0, "The mouth below the eyes must not glow");
    check(eyes[30 * W + 10] == 0 && eyes[8 * W + 14] == 0, "Plain skin must not glow");
    // Seen from behind: no eyes, only a red cable running diagonally.
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) paint(x, y, x < 20 ? 1 : 2);
    for (int k = 0; k < 8; ++k) paint(6 + k, 4 + k, 3);
    eyes = doom_sprite_eyes(W, H, pixels, palette);
    for (unsigned char e : eyes) check(e == 0, "A thin cable is not an eye");
    // The stock table holds only for the original artwork: a sprite of the
    // same name and size with other pixels falls back to detection.
    std::vector<unsigned char> zombie((size_t)41 * 55 * 2, 0), authored;
    check(!doom_stock_eyes("POSSA1", 41, 55, zombie.data(), authored), "Replaced artwork must not use the stock eyes");
    check(!doom_stock_eyes("NOTASPRT", W, H, pixels, authored), "Unknown sprites have no stock eyes");
    return 0;
}
