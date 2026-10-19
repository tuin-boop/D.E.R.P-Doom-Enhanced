/* Finding a monster's leg joints in its own drawing, for the procedural
   falls (scene3d/death_falls.cpp) where no hand rig exists
   (death_rigs_stock.h). The same rule as the death lab's finder
   (tests/joint_finder.py), which scores it against the hand rigs; front
   view, units right of the sprite's origin and above its feet:
   - Legs: every row near the feet with two runs of texels at least two
     wide may start them; each leg follows the run overlapping it in the row
     above until the two meet (the crotch) or one is lost, and the start
     tracking the longest legs wins (the lowest rows can be two toes of one
     foot). Legs touching for a few rows and parting again above (knock
     knees) are split where they parted below.
   - Heights from the leg's length (floor to crotch): the hip 35% of it
     above the crotch, the ankle 22% above the floor, the knee halfway.
   - Across: each leg's middle at the knee's and ankle's height, the hip
     over its top rows. A run jumping wider than a leg grows has something
     joined to it from outside (a gun): the leg keeps its width from the
     inner edge.
   - Carried with the torso: between the ankles and the hip's blend, texels outside the legs,
     grown into them above the knee through colours clearly nearer their own
     than the shins' (a gun crossing a thigh).
   No rig: fewer than 6 rows of legs, a crotch under 20% or over 60% of the
   body's height, or over a third of the lower body outside the legs (machine
   legs, an arm taken for a leg). */
#ifndef DOOM_DEATH_JOINTS_H
#define DOOM_DEATH_JOINTS_H
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include "death_rigs_stock.h"

namespace doom_joints {
typedef std::pair<int, int> Span;   // first and last column of a run of texels

struct Picture {
    const unsigned char *pixels;    // palette index and coverage pairs, row by row
    int width, height, left, top;
    bool solid(int c, int r) const { return pixels[((size_t)r * width + c) * 2 + 1] != 0; }
    std::vector<Span> runs(int r) const {
        std::vector<Span> out;
        for (int c = 0, start = -1; c <= width; ++c) {
            bool s = c < width && solid(c, r);
            if (s && start < 0) start = c;
            else if (!s && start >= 0) { out.push_back({start, c - 1}); start = -1; }
        }
        return out;
    }
};
inline int overlap(Span a, Span b) { return std::min(a.second, b.second) - std::max(a.first, b.first) + 1; }
inline const Span *best(const std::vector<Span> &spans, Span to)
{
    const Span *found = nullptr;
    for (const Span &s : spans) if (!found || overlap(s, to) > overlap(*found, to)) found = &s;
    return found;
}

inline void track(const Picture &pic, int first, int start, Span l, Span r0,
                  std::vector<Span> &left, std::vector<Span> &right)
{
    left.assign(1, l); right.assign(1, r0);
    int r = start - 1, bridged = -1;
    while (r >= first) {
        std::vector<Span> spans = pic.runs(r);
        const Span *nl = best(spans, left.back()), *nr = best(spans, right.back());
        if (!nl || !nr || overlap(*nl, left.back()) <= 0 || overlap(*nr, right.back()) <= 0) {
            if (bridged >= 0) { left.resize(bridged); right.resize(bridged); }
            break;
        }
        if (*nl == *nr) {
            int gap = (left.back().second + right.back().first + 1) / 2, above = -1;
            for (int k = 1; k <= 6 && r - k >= first; ++k) {
                std::vector<Span> up = pic.runs(r - k);
                const Span *a = best(up, left.back()), *b = best(up, right.back());
                if (a && b && *a != *b && overlap(*a, left.back()) > 0 && overlap(*b, right.back()) > 0) { above = k; break; }
            }
            if (above < 0 || !(nl->first < gap && gap <= nl->second)) break;
            Span merged = *nl;
            bridged = (int)left.size();
            for (int k = 0; k < above; ++k) {
                Span run = *best(pic.runs(r - k), merged);
                left.push_back({run.first, gap - 1}); right.push_back({gap, run.second});
            }
            r -= above;
            continue;
        }
        left.push_back(*nl); right.push_back(*nr);
        --r;
    }
}

// Each row's leg (clipped where something joined it from outside) and its middle in units.
inline void middles(const Picture &pic, const std::vector<Span> &track, bool innerRight,
                    std::vector<Span> &legs, std::vector<float> &mid)
{
    int width = track[0].second - track[0].first + 1;
    for (Span s : track) {
        if (s.second - s.first + 1 > width + 2)
            s = innerRight ? Span{s.second - width + 1, s.second} : Span{s.first, s.first + width - 1};
        width = s.second - s.first + 1;
        legs.push_back(s);
        mid.push_back((s.first + s.second + 1) / 2.0f - pic.left);
    }
}

// A rig for the drawing (joints into rig, the torso's texels into carried, one byte per
// texel), or false where the drawing isn't a body standing on two legs.
inline bool find(const unsigned char *pixels, int width, int height, int left, int top,
                 const unsigned char *palette, doom_stock_rig &rig, std::vector<unsigned char> &carried)
{
    Picture pic{pixels, width, height, left, top};
    int first = -1, bottom = -1;
    for (int r = 0; r < height; ++r)
        for (int c = 0; c < width; ++c) if (pic.solid(c, r)) { if (first < 0) first = r; bottom = r; break; }
    if (first < 0) return false;
    float body = (float)(top - first);
    int start = -1;
    std::vector<Span> L, R, tl, tr;
    for (int s = bottom; s >= std::max(first, bottom - 10); --s) {
        std::vector<Span> wide;
        for (const Span &sp : pic.runs(s)) if (sp.second - sp.first >= 1) wide.push_back(sp);
        if (wide.size() < 2) continue;
        std::stable_sort(wide.begin(), wide.end(), [](Span a, Span b) { return a.second - a.first > b.second - b.first; });
        Span a = wide[0], b = wide[1];
        if (b.first < a.first) std::swap(a, b);
        track(pic, first, s, a, b, tl, tr);
        if (start < 0 || tl.size() > L.size()) { start = s; L = tl; R = tr; }
    }
    if (start < 0) return false;
    int count = (int)L.size();
    float floor = (float)(top - start - 1), crotch = floor + count, length = (float)count;
    if (count < 6 || length < 0.2f * body || length > 0.6f * body) return false;
    float hip = crotch + 0.35f * length, ankle = floor + 0.22f * length, knee = (hip + ankle) / 2;

    std::vector<Span> legs[2];
    std::vector<float> mid[2];
    middles(pic, L, true, legs[0], mid[0]);
    middles(pic, R, false, legs[1], mid[1]);
    rig = doom_stock_rig{};
    for (int side = 0; side < 2; ++side) {
        auto at = [&](float h) { return mid[side][std::min(count - 1, std::max(0, (int)(h - floor)))]; };
        float hu = 0;
        int n = std::min(3, count);
        for (int k = count - n; k < count; ++k) hu += mid[side][k];
        rig.hip[side][0] = hu / n; rig.hip[side][1] = hip;
        rig.knee[side][0] = at(knee); rig.knee[side][1] = knee;
        rig.ankle[side][0] = at(ankle); rig.ankle[side][1] = ankle;
    }
    float gaps = 0;
    int n = std::min(3, count);
    for (int i = count - n; i < count; ++i) gaps += (L[i].second + 1 + R[i].first) / 2.0f - left;
    rig.split = gaps / n;
    rig.width = width; rig.height = height;

    // Carried: below the hip's blend, what lies outside the legs.
    carried.assign((size_t)width * height, 0);
    std::vector<std::pair<int, int>> todo;
    int lo = legs[0].back().first - 1, hi = legs[1].back().second + 1, lower = 0, held = 0;
    for (int r = 0; r < height; ++r) {
        float h = top - r - 0.5f;
        for (int c = 0; c < width; ++c) {
            if (!pic.solid(c, r) || h >= hip + 2) continue;
            ++lower;
            if (r > start || h <= ankle) continue;   // nothing hangs at the feet (a toe claw is no gun)
            int k = start - r;
            bool inside = k < count ? (legs[0][k].first <= c && c <= legs[0][k].second) || (legs[1][k].first <= c && c <= legs[1][k].second)
                                    : lo <= c && c <= hi;
            if (!inside) { carried[(size_t)r * width + c] = 1; todo.push_back({c, r}); ++held; }
        }
    }
    // Grown into the legs above the knee through colours nearer the carried ones' than the shins'.
    auto rgb = [&](int c, int r) { return palette + pixels[((size_t)r * width + c) * 2] * 3; };
    std::vector<const unsigned char *> shins, seeds;
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < count; ++k)
            if (ankle <= floor + k + 0.5f && floor + k + 0.5f <= knee)
                for (int c = legs[side][k].first; c <= legs[side][k].second; ++c) shins.push_back(rgb(c, start - k));
    for (auto &p : todo) seeds.push_back(rgb(p.first, p.second));
    auto nearest = [](const unsigned char *colour, const std::vector<const unsigned char *> &group) {
        float best = 1e9f;
        for (const unsigned char *g : group) {
            float d = std::hypot((float)colour[0] - g[0], std::hypot((float)colour[1] - g[1], (float)colour[2] - g[2]));
            best = std::min(best, d);
        }
        return best;
    };
    while (!todo.empty() && !shins.empty()) {
        std::pair<int, int> p = todo.back(); todo.pop_back();
        static const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &d : steps) {
            int x = p.first + d[0], y = p.second + d[1];
            if (x < 0 || y < 0 || x >= width || y >= height || !pic.solid(x, y) || carried[(size_t)y * width + x]) continue;
            float h = top - y - 0.5f;
            if (!(knee < h && h < hip + 2)) continue;
            if (nearest(rgb(x, y), seeds) < 0.5f * nearest(rgb(x, y), shins)) {
                carried[(size_t)y * width + x] = 1; todo.push_back({x, y}); ++held;
            }
        }
    }
    return held <= 0.35f * lower;
}

// The head, for telling a shot through it (the death lab's find_head): from the
// drawing's top down, the run under the head's middle, until a row widens past
// 1.6 times the head's first rows (the shoulders) within the top 40% of the body.
// Its middle and width are medians over the head's rows, so horns or a raised
// hand don't widen it. False where no head sits above shoulders (the demon).
struct Head { float neck, middle, width; };   // units: the neck's height, the head's middle and width
inline bool findHead(const unsigned char *pixels, int width, int height, int left, int top, Head &head)
{
    Picture pic{pixels, width, height, left, top};
    int first = -1;
    for (int r = 0; r < height && first < 0; ++r)
        for (int c = 0; c < width; ++c) if (pic.solid(c, r)) { first = r; break; }
    if (first < 0) return false;
    float body = (float)(top - first);
    auto span = [&](int r, int around, Span &out) {
        std::vector<Span> rs = pic.runs(r);
        if (rs.empty()) return false;
        int bestDistance = 1 << 30;
        for (const Span &s : rs) {
            int d = s.first <= around && around <= s.second ? 0 : std::min(std::abs(s.first - around), std::abs(s.second - around));
            if (d < bestDistance) { bestDistance = d; out = s; }
        }
        return true;
    };
    Span s;
    span(first, width / 2, s);
    int mid = (s.first + s.second) / 2;
    std::vector<Span> spans;
    for (int k = 0; k < (int)(body * 0.4f) && first + k < height; ++k) {
        if (!span(first + k, mid, s)) break;
        spans.push_back(s);
        mid = (s.first + s.second) / 2;
    }
    if (spans.size() < 6) return false;
    int ref = 0;
    for (int k = 0; k < 5; ++k) ref = std::max(ref, spans[k].second - spans[k].first + 1);
    for (size_t k = 5; k < spans.size(); ++k) {
        if (spans[k].second - spans[k].first + 1 <= 1.6f * ref) continue;
        std::vector<float> widths, middles;
        for (size_t i = 0; i < k; ++i) {
            widths.push_back((float)(spans[i].second - spans[i].first + 1));
            middles.push_back((spans[i].first + spans[i].second + 1) / 2.0f - left);
        }
        std::sort(widths.begin(), widths.end()); std::sort(middles.begin(), middles.end());
        head = Head{(float)(top - (first + (int)k)), middles[k / 2], widths[k / 2]};
        return true;
    }
    return false;
}
} // namespace doom_joints
#endif
