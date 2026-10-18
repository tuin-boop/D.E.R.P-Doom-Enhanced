/* The player's weapon relit at runtime (weaponDraws in scene3d/weapon.cpp). Doom's
   weapon artwork carries painted light: bright sheen streaks (the pistol's
   slide, the chaingun's chrome) and a broad light-to-dark sweep across each
   part. doom_weapon_delight takes most of it back out of the palette indices,
   keeping texture detail, dark outlines and the palette's own ramps, and
   derives what the shader relights the frame with: normals from the
   silhouette and the painted shading, and gloss where the artist painted
   highlights or the metal runs grey. */
#ifndef DOOM_WEAPON_LIGHTING_H
#define DOOM_WEAPON_LIGHTING_H
#include <algorithm>
#include <cmath>
#include <vector>

struct doom_weapon_maps {
    std::vector<unsigned char> pixels;  /* Palette index and coverage pairs, de-lit. */
    std::vector<unsigned char> normals; /* x and y (texture down) as unsigned bytes. */
    std::vector<unsigned char> gloss;   /* 0-255 per pixel. */
};

/* pixels: palette index and coverage pairs; palette: PLAYPAL RGB. */
inline doom_weapon_maps doom_weapon_delight(int w, int h, const unsigned char *pixels, const unsigned char *palette)
{
    const size_t n = (size_t)w * h;
    doom_weapon_maps maps;
    maps.pixels.assign(pixels, pixels + n * 2);
    maps.normals.assign(n * 2, 128);
    maps.gloss.assign(n, 0);
    auto opaque = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && pixels[((size_t)y * w + x) * 2 + 1]; };
    auto rgb = [&](int index, int c) { return palette[index * 3 + c] / 255.0f; };
    auto luma = [&](int index) { return 0.299f * rgb(index, 0) + 0.587f * rgb(index, 1) + 0.114f * rgb(index, 2); };
    /* Chromaticity: which ramp a color sits on, independent of its brightness. */
    auto chroma = [&](int index, float &r, float &g) {
        float sum = rgb(index, 0) + rgb(index, 1) + rgb(index, 2) + 1e-4f;
        r = rgb(index, 0) / sum; g = rgb(index, 1) / sum;
    };
    std::vector<float> Y(n, 0), cr(n, 0), cg(n, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (opaque(x, y)) {
        size_t i = (size_t)y * w + x; int index = pixels[i * 2];
        Y[i] = luma(index); chroma(index, cr[i], cg[i]);
    }

    /* Sheen: texels brighter than the darker 40% of a 13x13 window keep a
       quarter of the excess. A low percentile also sees under broad chrome
       highlights; darker texels (outlines, crevices) stay as painted. */
    const int r = 6;
    std::vector<float> flat(Y), removed(n, 0);
    for (int y = 0; y < h; ++y) {
        int histogram[256] = {0}, count = 0;
        auto column = [&](int x, int add) {
            if (x < 0 || x >= w) return;
            for (int j = std::max(0, y - r); j <= std::min(h - 1, y + r); ++j) if (opaque(x, j)) {
                histogram[std::min(255, (int)(Y[(size_t)j * w + x] * 255.0f))] += add; count += add;
            }
        };
        for (int x = -r; x < r; ++x) column(x, 1);
        for (int x = 0; x < w; ++x) {
            column(x + r, 1); column(x - r - 1, -1);
            if (!opaque(x, y) || count <= 0) continue;
            int target = count * 2 / 5, seen = 0, bin = 0;
            for (; bin < 255; ++bin) if ((seen += histogram[bin]) > target) break;
            size_t i = (size_t)y * w + x;
            float base = (bin + 0.5f) / 255.0f, excess = Y[i] - base;
            if (excess > 0) { flat[i] = base + excess * 0.25f; removed[i] = excess * 0.75f; }
        }
    }

    /* The broad sweep: each texel's neighborhood on its own ramp (a blur
       weighted by chromaticity, near-black texels joining any ramp) against
       that ramp's mean over a wider window. Pulling the first 60% of the way
       to the second flattens painted form light but not the texture's own
       variation. The same pass averages the sheen removed on the ramp, which
       becomes its gloss. Samples every other texel; the blur is broad. */
    const int R = 12;
    float spatial[2 * R + 1][2 * R + 1];
    for (int j = -R; j <= R; ++j) for (int i = -R; i <= R; ++i) spatial[j + R][i + R] = std::exp(-(i * i + j * j) / 72.0f);
    std::vector<float> target(flat), glossRamp(n, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        size_t i = (size_t)y * w + x;
        if (!opaque(x, y)) continue;
        bool dark = Y[i] < 0.06f;
        float local = 0, localWeight = 0, ramp = 0, rampWeight = 0, sheen = 0;
        for (int j = y - R + ((y ^ x) & 1); j <= y + R; j += 2) for (int k = x - R; k <= x + R; k += 2) {
            if (!opaque(k, j)) continue;
            size_t s = (size_t)j * w + k;
            float dr = cr[i] - cr[s], dg = cg[i] - cg[s];
            float same = dark || Y[s] < 0.06f ? 1.0f : std::exp(-(dr * dr + dg * dg) / 0.0016f);
            float near = spatial[j - y + R][k - x + R] * same;
            local += flat[s] * near; localWeight += near;
            ramp += flat[s] * same; rampWeight += same; sheen += removed[s] * same;
        }
        if (localWeight <= 0 || rampWeight <= 0) continue;
        float ratio = std::pow((ramp / rampWeight) / std::max(local / localWeight, 1e-3f), 0.6f);
        target[i] = flat[i] * std::clamp(ratio, 0.7f, 1.4f);
        glossRamp[i] = sheen / rampWeight;
    }

    /* Back to the palette, on the texel's own ramp so the shader's palette
       effects still apply. Ramps are runs of consecutive entries that darken
       at a steady hue (near black, at any hue), as COLORMAP-era palettes lay
       them out; a lone entry may move to the nearest one of a close hue. */
    int ramp[256];
    ramp[0] = 0;
    for (int p = 1; p < 256; ++p) {
        float r0, g0, r1, g1; chroma(p - 1, r0, g0); chroma(p, r1, g1);
        bool steady = (r1 - r0) * (r1 - r0) + (g1 - g0) * (g1 - g0) < 0.006f || luma(p) < 0.1f;
        ramp[p] = luma(p) <= luma(p - 1) + 0.004f && steady ? ramp[p - 1] : p;
    }
    std::vector<short> memo(256 * 256, -1);
    for (size_t i = 0; i < n; ++i) {
        if (!pixels[i * 2 + 1] || Y[i] < 0.02f || std::fabs(target[i] - Y[i]) < 0.01f) continue;
        int index = pixels[i * 2], level = std::clamp((int)std::lround(target[i] * 255.0f), 0, 255);
        short &cached = memo[index * 256 + level];
        if (cached < 0) {
            float scale = target[i] / Y[i], want[3], best = 1e30f, r0, g0;
            for (int c = 0; c < 3; ++c) want[c] = rgb(index, c) * scale;
            chroma(index, r0, g0);
            bool lone = (index == 255 || ramp[index + 1] != ramp[index]) && ramp[index] == index;
            cached = (short)index;
            for (int p = 0; p < 256; ++p) {
                float r1, g1; chroma(p, r1, g1);
                if (lone ? luma(p) > 0.04f && (r1 - r0) * (r1 - r0) + (g1 - g0) * (g1 - g0) > 0.0012f : ramp[p] != ramp[index]) continue;
                float d0 = rgb(p, 0) - want[0], d1 = rgb(p, 1) - want[1], d2 = rgb(p, 2) - want[2];
                float d = 2 * d0 * d0 + 4 * d1 * d1 + 3 * d2 * d2;
                if (d < best) { best = d; cached = (short)p; }
            }
        }
        maps.pixels[i * 2] = (unsigned char)cached;
    }

    /* Normals: a rounded rim from the silhouette (the
       frame's bottom edge is cut off by the screen, not an outline), tilted by
       the painted shading read as height, bright standing proud (blurred,
       so the artwork's dithering does not turn into grit). */
    std::vector<float> distance(n);
    for (size_t i = 0; i < n; ++i) distance[i] = pixels[i * 2 + 1] ? 1e6f : 0;
    auto at = [&](int x, int y) {
        if (x < 0 || x >= w || y < 0) return 0.0f;
        return y >= h ? 1e6f : distance[(size_t)y * w + x];
    };
    for (int pass = 0; pass < 2; ++pass) {
        int step = pass ? -1 : 1;
        for (int y = pass ? h - 1 : 0; y >= 0 && y < h; y += step) for (int x = pass ? w - 1 : 0; x >= 0 && x < w; x += step) {
            float &d = distance[(size_t)y * w + x]; if (d == 0) continue;
            d = std::min({d, at(x - step, y) + 1, at(x, y - step) + 1, at(x - step, y - step) + 1.4142f, at(x + step, y - step) + 1.4142f});
        }
    }
    float radius = std::clamp(std::min(w, h) * 0.25f, 3.0f, 12.0f);
    auto dome = [&](int x, int y) {
        float t = std::min(1.0f, at(std::clamp(x, -1, w), std::min(y, h)) / radius);
        return std::sqrt(std::max(0.0f, 1 - (1 - t) * (1 - t)));
    };
    std::vector<float> relief(n, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!opaque(x, y)) continue;
        float sum = 0, weight = 0;
        static const float binomial[5] = {1, 4, 6, 4, 1};
        for (int j = -2; j <= 2; ++j) for (int k = -2; k <= 2; ++k) if (opaque(x + k, y + j)) {
            float s = binomial[j + 2] * binomial[k + 2];
            sum += Y[(size_t)(y + j) * w + x + k] * s; weight += s;
        }
        relief[(size_t)y * w + x] = sum / weight;
    }
    auto height = [&](int x, int y, int fx, int fy) {
        return opaque(x, y) ? relief[(size_t)y * w + x] : relief[(size_t)fy * w + fx];
    };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!opaque(x, y)) continue;
        size_t i = (size_t)y * w + x;
        float gx = (dome(x + 1, y) - dome(x - 1, y)) * 0.5f * radius + (height(x + 1, y, x, y) - height(x - 1, y, x, y)) * 2.0f;
        float gy = (dome(x, y + 1) - dome(x, y - 1)) * 0.5f * radius + (height(x, y + 1, x, y) - height(x, y - 1, x, y)) * 2.0f;
        float length = std::sqrt(gx * gx + gy * gy + 1);
        maps.normals[i * 2] = (unsigned char)std::lround((-gx / length * 0.5f + 0.5f) * 255);
        maps.normals[i * 2 + 1] = (unsigned char)std::lround((-gy / length * 0.5f + 0.5f) * 255);
        /* Gloss: the sheen the ramp lost around here, and grey metal; skin
           and other saturated ramps keep a third of their painted sheen's. */
        int index = pixels[i * 2];
        float hi = std::max({rgb(index, 0), rgb(index, 1), rgb(index, 2)});
        float lo = std::min({rgb(index, 0), rgb(index, 1), rgb(index, 2)});
        float grey = hi > 0.05f ? 1 - std::clamp((hi - lo) / hi * 2.5f, 0.0f, 1.0f) : 0.0f;
        float painted = std::clamp(glossRamp[i] * 10.0f + removed[i] * 2.0f, 0.0f, 1.0f) * (0.35f + 0.65f * grey);
        float gloss = std::max(painted, grey * 0.5f);
        maps.gloss[i] = (unsigned char)std::lround(gloss * 255);
    }
    return maps;
}
#endif
