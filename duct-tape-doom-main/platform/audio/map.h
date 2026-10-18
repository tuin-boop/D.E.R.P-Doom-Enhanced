/* What the sound code reads of the map: the lines around the listener and
 * where a straight path crosses them. Shared by room.c and occlusion.c. */
#ifndef DOOM_AUDIO_MAP_H
#define DOOM_AUDIO_MAP_H
#include "doomdef.h"
#include "doomstat.h"
#include "r_state.h"

#define MAP_REACH 2560.0f  /* map units around the listener the sound code sees */
#define MAP_MIN_GAP 40.0f  /* openings lower than this stop sound like a wall */

static inline float Map_Floor(const sector_t *s) { return s->floorheight / (float)FRACUNIT; }
static inline float Map_Ceiling(const sector_t *s) { return s->ceilingheight / (float)FRACUNIT; }
static inline int Map_Sky(const sector_t *s) { return s->ceilingpic == skyflatnum; }

/* A line a path crosses, t units along it. One-sided lines have no far sector. */
typedef struct { float t; line_t *line; sector_t *near, *far; int near_side; } crossing_t;

/* Collects the lines within MAP_REACH of (x, y); cheap to repeat at one spot. */
void Map_Gather(float x, float y);
/* The gathered lines crossed walking from (x, y) along the unit direction
   (cx, cy) for length units, nearest first; at most max of them. */
int Map_Cross(float x, float y, float cx, float cy, float length, crossing_t *out, int max);
/* The open height between two sectors; both under sky are open to the top. */
static inline void Map_Gap(const sector_t *near, const sector_t *far, float *bottom, float *top)
{
    float a = Map_Ceiling(near), b = Map_Ceiling(far);
    *top = Map_Sky(near) && Map_Sky(far) ? 1e9f : a < b ? a : b;
    a = Map_Floor(near); b = Map_Floor(far);
    *bottom = a > b ? a : b;
}
#endif
