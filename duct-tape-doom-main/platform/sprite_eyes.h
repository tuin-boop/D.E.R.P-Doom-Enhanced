/* Glowing eyes found in monster artwork (lumpImage in scene3d/images.cpp).
   Doom's artists painted most monsters' eyes in a pure palette ramp the
   body around them never uses: the zombies' and imp's red, the demon's
   yellow, the cacodemon's and mancubus's green, the arachnotron's blue.
   doom_sprite_eyes finds them in one frame: small, bright, enclosed spots
   in the upper body whose tint (color over brightness, so shading drops
   out) differs from the colored pixels around them on every side, the
   most distinct group, and of those the highest (eyes sit above mouths
   and wounds). Each spot then spreads into its own darker ring of the
   same tint, and spots that came out as lines (cables, stripes) are
   dropped. Frames without eyes in view (backs, profiles) yield nothing. */
#ifndef DOOM_SPRITE_EYES_H
#define DOOM_SPRITE_EYES_H
#include <algorithm>
#include <cmath>
#include <vector>

/* pixels: palette index and coverage pairs; palette: PLAYPAL RGB.
   Returns per pixel 0, or the glow: 255 at a spot, 170 where it spread. */
inline std::vector<unsigned char> doom_sprite_eyes(int w, int h, const unsigned char *pixels, const unsigned char *palette)
{
    const size_t n = (size_t)w * h;
    std::vector<unsigned char> eyes(n, 0);
    std::vector<float> rgb(n * 3);
    int top = h, bottom = -1, left = w, right = -1;
    size_t area = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        size_t i = (size_t)y * w + x;
        if (!pixels[i * 2 + 1]) continue;
        for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = palette[pixels[i * 2] * 3 + c] / 255.0f;
        top = std::min(top, y); bottom = std::max(bottom, y); left = std::min(left, x); right = std::max(right, x);
        ++area;
    }
    if (!area) return eyes;
    const int height = bottom - top + 1;
    // Upright figures carry their heads in the top quarter; squat and
    // round ones (demon, cacodemon, spiders) lower.
    const float limit = (height >= 1.5f * (right - left + 1) ? 0.27f : 0.45f) * height;
    auto opaque = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && pixels[((size_t)y * w + x) * 2 + 1]; };
    auto at = [&](int x, int y) { return &rgb[((size_t)y * w + x) * 3]; };
    auto peak = [](const float *c) { return std::max(c[0], std::max(c[1], c[2])); };
    auto colored = [&](int x, int y) {
        if (!opaque(x, y)) return false;
        const float *c = at(x, y);
        float hi = peak(c), lo = std::min(c[0], std::min(c[1], c[2]));
        return hi >= 0.15f && (hi - lo) / hi >= 0.25f;
    };
    // Tint: the color over its brightest channel. Chroma: the color minus its grey.
    auto tint = [&](const float *c, float *t) { float p = std::max(peak(c), 1e-6f); for (int k = 0; k < 3; ++k) t[k] = c[k] / p; };
    auto chroma = [&](const float *c, float *v) { float m = (c[0] + c[1] + c[2]) / 3; for (int k = 0; k < 3; ++k) v[k] = c[k] - m; };
    auto hue = [&](const float *c) {
        float hi = peak(c), lo = std::min(c[0], std::min(c[1], c[2])), d = hi - lo, h6;
        if (d <= 0) return 0.0f;
        if (hi == c[0]) h6 = std::fmod((c[1] - c[2]) / d + 6, 6.0f);
        else if (hi == c[1]) h6 = (c[2] - c[0]) / d + 2;
        else h6 = (c[0] - c[1]) / d + 4;
        return h6 * 60;
    };
    auto median = [](std::vector<float> &values) {
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        return values[values.size() / 2];
    };
    auto distance = [](const float *a, const float *b) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    };
    // Per pixel: how far its tint lies from the colored ring 2-4 pixels
    // around (the least over the ring's four halves, so the edge between
    // two materials does not count), and how much more color it carries
    // than the ring in its own direction (a pale highlight carries less).
    std::vector<float> apart(n, -1), lift(n, 0), hues(n, 0);
    std::vector<float> channel[3], side[4][3];
    for (int y = top; y <= bottom && y - top <= limit + 2; ++y) for (int x = left; x <= right; ++x) {
        if (!opaque(x, y)) continue;
        const float *c = at(x, y);
        float v[3]; chroma(c, v);
        float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (peak(c) < 0.5f || length < 0.25f) continue;
        bool enclosed = true;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) enclosed = enclosed && opaque(x + dx, y + dy);
        if (!enclosed) continue;
        for (int k = 0; k < 3; ++k) { channel[k].clear(); for (auto &s : side) s[k].clear(); }
        std::vector<float> around[3];
        for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) < 2 || !opaque(x + dx, y + dy)) continue;
            float u[3]; chroma(at(x + dx, y + dy), u);
            for (int k = 0; k < 3; ++k) around[k].push_back(u[k]);
            if (!colored(x + dx, y + dy)) continue;
            float t[3]; tint(at(x + dx, y + dy), t);
            for (int k = 0; k < 3; ++k) {
                channel[k].push_back(t[k]);
                if (dx < 0) side[0][k].push_back(t[k]);
                if (dx > 0) side[1][k].push_back(t[k]);
                if (dy < 0) side[2][k].push_back(t[k]);
                if (dy > 0) side[3][k].push_back(t[k]);
            }
        }
        if (channel[0].size() < 8) continue;
        float own[3], ring[3]; tint(c, own);
        for (int k = 0; k < 3; ++k) ring[k] = median(channel[k]);
        float least = distance(own, ring);
        for (auto &s : side) {
            if (s[0].size() < 4) continue;
            float half[3]; for (int k = 0; k < 3; ++k) half[k] = median(s[k]);
            least = std::min(least, distance(own, half));
        }
        float base[3]; for (int k = 0; k < 3; ++k) base[k] = median(around[k]);
        size_t i = (size_t)y * w + x;
        apart[i] = least; hues[i] = hue(c);
        lift[i] = ((v[0] - base[0]) * v[0] + (v[1] - base[1]) * v[1] + (v[2] - base[2]) * v[2]) / length;
    }
    // Spots: candidates joined while their hues stay within 45 degrees.
    auto candidate = [&](size_t i) { return apart[i] >= 0.3f && lift[i] >= 0.18f; };
    auto hueGap = [](float a, float b) { float d = std::fabs(a - b); return std::min(d, 360 - d); };
    struct Spot { std::vector<size_t> pixels; float y, score; };
    std::vector<Spot> spots;
    std::vector<char> seen(n, 0);
    for (size_t s = 0; s < n; ++s) {
        if (seen[s] || apart[s] < 0 || !candidate(s)) continue;
        Spot spot{{s}, 0, 0}; seen[s] = 1;
        for (size_t k = 0; k < spot.pixels.size(); ++k) {
            size_t a = spot.pixels[k]; int ax = int(a % w), ay = int(a / w);
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if (!opaque(ax + dx, ay + dy)) continue;
                size_t b = (size_t)(ay + dy) * w + ax + dx;
                if (seen[b] || apart[b] < 0 || !candidate(b) || hueGap(hues[a], hues[b]) > 45) continue;
                seen[b] = 1; spot.pixels.push_back(b);
            }
        }
        float mean[3] = {}, bright = 0;
        for (size_t p : spot.pixels) {
            float v[3]; chroma(&rgb[p * 3], v);
            for (int k = 0; k < 3; ++k) mean[k] += v[k] / spot.pixels.size();
            bright = std::max(bright, peak(&rgb[p * 3]));
            spot.y += float(p / w) / spot.pixels.size();
            spot.score = std::max(spot.score, apart[p]);
        }
        if (spot.pixels.size() > std::max<size_t>(4, area / 100) || spot.y - top > limit) continue;
        if (std::sqrt(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]) < 0.3f || bright < 0.65f) continue;
        spots.push_back(spot);
    }
    if (spots.empty()) return eyes;
    float best = 0, highest = 1e9f;
    for (const Spot &s : spots) best = std::max(best, s.score);
    for (const Spot &s : spots) if (s.score >= 0.6f * best) highest = std::min(highest, s.y);
    for (const Spot &s : spots) {
        if (s.score < 0.6f * best || s.y > highest + 0.03f * height + 1.5f) continue;
        // Spread into the spot's own ring: still apart from the body, close
        // in tint to the pixel it spreads from, at most three times its size.
        std::vector<size_t> eye = s.pixels;
        std::vector<char> in(n, 0);
        for (size_t p : eye) in[p] = 1;
        for (size_t k = 0; k < eye.size() && eye.size() < 3 * s.pixels.size() + 4; ++k) {
            size_t a = eye[k]; int ax = int(a % w), ay = int(a / w);
            float ta[3]; tint(&rgb[a * 3], ta);
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if (!opaque(ax + dx, ay + dy)) continue;
                size_t b = (size_t)(ay + dy) * w + ax + dx;
                if (in[b] || apart[b] < 0.15f || lift[b] < 0 || eye.size() >= 3 * s.pixels.size() + 4) continue;
                float tb[3]; tint(&rgb[b * 3], tb);
                if (distance(ta, tb) > 0.3f) continue;
                in[b] = 1; eye.push_back(b);
            }
        }
        int x0 = w, x1 = -1, y0 = h, y1 = -1;
        for (size_t p : eye) { x0 = std::min(x0, int(p % w)); x1 = std::max(x1, int(p % w)); y0 = std::min(y0, int(p / w)); y1 = std::max(y1, int(p / w)); }
        int across = x1 - x0 + 1, down = y1 - y0 + 1;
        if (std::max(across, down) > 2 * std::min(across, down) + 2 || eye.size() < 0.35f * across * down) continue;
        for (size_t k = 0; k < eye.size(); ++k) eyes[eye[k]] = k < s.pixels.size() ? 255 : 170;
    }
    return eyes;
}
#endif
