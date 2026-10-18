/* Muffling. A sound in plain sight only loses treble with distance. One in
 * the listener's own sector is checked straight: over a ledge or round a
 * pillar it is muffled a little. One elsewhere is heard the way the map's
 * openings let it through (acoustic_graph.h), the same ways the ambience
 * takes: dull through a shut door, a slit or a window's glass, and the
 * further round the corners its way goes against straight, the duller. The
 * volume stays vanilla's, so hearing still tells as much as ever about where
 * monsters are. */
#include <math.h>
#include <stdlib.h>
#include "p_local.h"
#include "acoustic_graph.h"
#include "map.h"
#include "occlusion.h"

#define EAR 41.0f
#define NEAR 200.0f       /* closer than this, distance takes nothing */
#define FAR 1200.0f       /* vanilla's clipping distance */
#define FAR_HZ 3500.0f
#define LAST_HZ 2500.0f   /* E?M8 sounds carry farther still */
#define AROUND_HZ 1800.0f /* over a ledge, or a way round half again as long as straight */
#define SEALED_HZ 500.0f  /* no way found at all: solid wall */
#define DETOUR_FREE 1.05f /* ways this much longer than straight are as good as straight */
#define DETOUR_FULL 1.5f  /* this much longer, as dull as AROUND_HZ */
#define MAX_CROSSINGS 128

static float distance_cutoff(float distance)
{
    if (distance <= NEAR) return OCCLUSION_OPEN_HZ;
    float cutoff = OCCLUSION_OPEN_HZ * powf(FAR_HZ / OCCLUSION_OPEN_HZ, (distance - NEAR) / (FAR - NEAR));
    return cutoff > LAST_HZ ? cutoff : LAST_HZ;
}

/* Sound leaks through any gap at all, if muffled through narrow ones; only
   shut doors and lifts stop it. A shut one sounds from inside its own closed
   sector, and sound that reaches it counts as having arrived. */
static int open_gap(float bottom, float top) { return top - bottom >= 1; }
static int shut(const sector_t *s) { return Map_Ceiling(s) - Map_Floor(s) < 1; }

/* What lies on the straight line from ear to sound: SIGHT nothing, LEDGE only
   openings it passes above or below (a window sill, a lintel) or narrow
   slits, WALL a wall or closed door. */
enum { SIGHT, LEDGE, WALL };
static int in_sight(float x, float y, float ear, const sector_t *there, float sx, float sy, float sz, float distance)
{
    static crossing_t hits[MAX_CROSSINGS];
    float cx = (sx - x) / distance, cy = (sy - y) / distance;
    int count = Map_Cross(x, y, cx, cy, distance, hits, MAX_CROSSINGS), seen = SIGHT;
    for (int i = 0; i < count; ++i)
    {
        if (!hits[i].near || !hits[i].far) return WALL;
        if (hits[i].far == there && shut(there)) return seen;
        float bottom, top, z = ear + (sz - ear) * hits[i].t / distance;
        Map_Gap(hits[i].near, hits[i].far, &bottom, &top);
        if (!open_gap(bottom, top)) return WALL;
        if (top - bottom < MAP_MIN_GAP || z <= bottom || z >= top) seen = LEDGE;
    }
    return seen;
}

float Occlusion_Cutoff(fixed_t x, fixed_t y, fixed_t z, int walls, int distance)
{
    mobj_t *listener = players[consoleplayer].mo;
    if (gamestate != GS_LEVEL || !listener || !listener->subsector || !numlines) return OCCLUSION_OPEN_HZ;
    float lx = listener->x / (float)FRACUNIT, ly = listener->y / (float)FRACUNIT;
    float sx = x / (float)FRACUNIT, sy = y / (float)FRACUNIT;
    float apart = hypotf(sx - lx, sy - ly);
    float cutoff = distance ? distance_cutoff(apart) : OCCLUSION_OPEN_HZ;
    if (!walls || apart < 1 || apart > MAP_REACH) return cutoff;

    /* Things sound from about their middle; sector sounds (doors, lifts) from
       the middle of their sector's height. */
    sector_t *here = listener->subsector->sector, *there = R_PointInSubsector(x, y)->sector;
    if (here == there)
    {
        float floor = Map_Floor(there), ceiling = Map_Ceiling(there);
        float sz = z / (float)FRACUNIT + 32;
        if (sz < floor + 8 || sz > ceiling - 8) sz = 0.5f * (floor + ceiling);
        float ear = listener->z / (float)FRACUNIT + EAR;
        Map_Gather(lx, ly);
        if (in_sight(lx, ly, ear, there, sx, sy, sz, apart) == SIGHT) return cutoff;
        return AROUND_HZ < cutoff ? AROUND_HZ : cutoff;
    }
    Acoustic_Follow();
    acoustic_way_t way;
    if (!Acoustic_Way(sx, sy, (int)(there - sectors), &way)) return SEALED_HZ < cutoff ? SEALED_HZ : cutoff;
    /* The longer the way round against straight, the duller. */
    float detour = (way.length / apart - DETOUR_FREE) / (DETOUR_FULL - DETOUR_FREE);
    detour = detour < 0 ? 0 : detour > 1 ? 1 : detour;
    float muffled = way.cutoff;
    float around = OCCLUSION_OPEN_HZ * powf(AROUND_HZ / OCCLUSION_OPEN_HZ, detour);
    if (around < muffled) muffled = around;
    return muffled < cutoff ? muffled : cutoff;
}
