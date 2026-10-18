// Data-free checks for weapon de-lighting (platform/weapon_lighting.h).
#include "../platform/weapon_lighting.h"
#include <cstdio>
#include <cstdlib>

static void check(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
int main()
{
    // Entries 0-31: a grey ramp from white down; 32-47: a skin ramp.
    unsigned char palette[768] = {0};
    for (int i = 0; i < 32; ++i) palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = (unsigned char)(250 - i * 7);
    for (int i = 0; i < 16; ++i) {
        palette[(32 + i) * 3] = (unsigned char)(230 - i * 10);
        palette[(32 + i) * 3 + 1] = (unsigned char)(150 - i * 7);
        palette[(32 + i) * 3 + 2] = (unsigned char)(100 - i * 5);
    }
    enum { W = 40, H = 40 };
    static unsigned char pixels[W * H * 2];
    // Grey metal in the top half with a painted sheen streak, skin below,
    // a transparent margin left and right, cut off by the bottom edge.
    for (int y = 0; y < H; ++y) for (int x = 6; x < W - 6; ++x) {
        unsigned char *p = &pixels[(y * W + x) * 2];
        p[0] = y < 20 ? (x >= 14 && x < 17 ? 2 : 20) : 38;
        p[1] = 255;
    }
    doom_weapon_maps maps = doom_weapon_delight(W, H, pixels, palette);
    int streak = maps.pixels[(10 * W + 15) * 2], plain = maps.pixels[(10 * W + 24) * 2];
    check(streak > 2 && streak < 32, "The sheen streak must darken along the grey ramp");
    check(plain >= 16 && plain < 32, "Plain metal must stay on the grey ramp");
    int skin = maps.pixels[(30 * W + 20) * 2];
    check(skin >= 32 && skin < 48, "Skin must stay on its ramp");
    check(maps.pixels[(10 * W + 2) * 2 + 1] == 0, "Coverage must not change");
    check(maps.normals[(10 * W + 6) * 2] < 128 && maps.normals[(10 * W + W - 7) * 2] > 128, "Silhouette edges must face outward");
    check(maps.normals[((H - 1) * W + 20) * 2 + 1] < 150, "The screen-cut bottom edge must not round off");
    check(maps.gloss[10 * W + 20] > maps.gloss[30 * W + 20], "Metal with painted sheen must be glossier than skin");
    return 0;
}
