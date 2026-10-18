/* Measures the room around the listener from the map. Rays across the
 * map find the walls: one-sided lines, and openings too low to let
 * sound through (closed doors, raised lifts). Their distances give the
 * echoes; the room's floor area, height, sky share and the materials its
 * textures are named after give the tail, by Sabine's formula squeezed into
 * a game-like range: corridors slap, halls boom, the outdoors stays dry. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "r_data.h"
#include "w_wad.h"
#include "map.h"
#include "room.h"

#define RAYS 32
#define SPEED 10700.0f  /* sound, in map units per second (a unit is ~3.2 cm) */
#define METERS 0.032f
#define EAR 41.0f       /* the listener's ear above their feet */
#define MAX_HITS 256

extern int numtextures, numflats;

/* absorb: share of sound a bounce loses; dark: how much of its treble. */
typedef struct { float absorb, dark; } material_t;
static const material_t metal = {0.06f, 0.15f}, stone = {0.10f, 0.45f}, wood = {0.22f, 0.70f},
    flesh = {0.30f, 0.80f}, liquid = {0.03f, 0.30f}, soil = {0.40f, 0.75f}, open_air = {1, 0};

static int prefixed(const char *name, const char *const *list)
{
    for (; *list; ++list)
    {
        size_t i = 0;
        while ((*list)[i] && toupper((unsigned char)name[i]) == (*list)[i]) ++i;
        if (!(*list)[i]) return 1;
    }
    return 0;
}
/* The IWADs' texture families; PWAD names that match none sound like stone. */
static material_t wall_material(const char *name)
{
    static const char *const metals[] = {"STARTAN", "STARG", "STARBR", "COMP", "METAL", "SUPPORT",
        "SHAWN", "DOOR", "BIGDOOR", "EXITDOOR", "LITE", "PIPE", "PLAT", "TEKWALL", "TEKGREN", "TEKLITE",
        "SLADWALL", "SILVER", "BRONZE", "SPACEW", "NUKE", "SW1", "SW2", "STEP", "MIDGRATE", "MIDBARS",
        "ICKDOOR", "ICKWALL", "TEKBRON", "BROWNPIP", NULL};
    static const char *const woods[] = {"WOOD", "PANEL", "PANBOOK", "PANBORD", "PANCASE", NULL};
    static const char *const fleshes[] = {"SKIN", "SKSNAKE", "SKSPINE", "SP_FACE", "FLESH", "SLOPPY", "SPCDOOR", NULL};
    if (prefixed(name, woods)) return wood;
    if (prefixed(name, fleshes)) return flesh;
    if (prefixed(name, metals)) return metal;
    return stone;
}
static material_t flat_material(const char *name)
{
    static const char *const liquids[] = {"NUKAGE", "FWATER", "SWATER", "SLIME", "BLOOD", "LAVA", "WATER", NULL};
    static const char *const soils[] = {"GRASS", "RROCK19", "RROCK20", "MFLR8_3", "FLAT10", NULL};
    static const char *const woods[] = {"FLAT5_1", "FLAT5_2", "CEIL1_1", "CRATOP", NULL};
    static const char *const metals[] = {"FLOOR4", "FLOOR5", "FLAT14", "FLAT20", "FLAT22", "FLAT23",
        "CEIL3", "COMP", "GATE", "STEP", "TLITE", "SLIME13", "CONS1", NULL};
    if (prefixed(name, liquids)) return liquid;
    if (prefixed(name, soils)) return soil;
    if (prefixed(name, woods)) return wood;
    if (prefixed(name, metals)) return metal;
    return stone;
}

/* Per texture and flat, named once per WAD. */
static material_t *walls, *flats;
static void name_materials(void)
{
    if (walls) return;
    walls = malloc(sizeof(*walls) * (numtextures > 0 ? numtextures : 1));
    flats = malloc(sizeof(*flats) * (numflats > 0 ? numflats : 1));
    char name[9];
    for (int i = 0; i < numtextures; ++i) { R_TextureName(i, name); walls[i] = wall_material(name); }
    for (int i = 0; i < numflats; ++i)
    {
        memcpy(name, lumpinfo[firstflat + i].name, 8); name[8] = 0;
        flats[i] = flat_material(name);
    }
}
static material_t wall(int texture) { return texture > 0 && texture < numtextures ? walls[texture] : stone; }
static material_t flat(int pic) { return pic >= 0 && pic < numflats ? flats[pic] : stone; }

/* One ray: how far it gets, off what, and the floors and ceilings it crosses. */
typedef struct {
    float distance; material_t surface;
    float length, sky_length, height, floor_absorb, floor_dark, ceiling_absorb, ceiling_dark;
} ray_t;

static void cross_sector(ray_t *ray, sector_t *s, float length)
{
    material_t f = flat(s->floorpic), c = Map_Sky(s) ? open_air : flat(s->ceilingpic);
    ray->length += length;
    if (Map_Sky(s)) ray->sky_length += length;
    ray->height += length * (Map_Ceiling(s) - Map_Floor(s));
    ray->floor_absorb += length * f.absorb; ray->floor_dark += length * f.dark;
    ray->ceiling_absorb += length * c.absorb; ray->ceiling_dark += length * c.dark;
}

static ray_t cast(float x, float y, float cx, float cy, sector_t *start)
{
    static crossing_t hits[MAX_HITS];
    int count = Map_Cross(x, y, cx, cy, MAP_REACH, hits, MAX_HITS);
    ray_t ray = {MAP_REACH, open_air};
    sector_t *here = start;
    float travelled = 0;
    for (int i = 0; i < count; ++i)
    {
        const crossing_t *hit = &hits[i];
        cross_sector(&ray, here, hit->t - travelled);
        travelled = hit->t;
        line_t *line = hit->line;
        int side = line->sidenum[hit->near_side] >= 0 ? line->sidenum[hit->near_side] : line->sidenum[0];
        if (!hit->near || !hit->far)
        {
            ray.distance = travelled; ray.surface = side >= 0 ? wall(sides[side].midtexture) : stone;
            return ray;
        }
        float bottom, top;
        Map_Gap(hit->near, hit->far, &bottom, &top);
        if (top - bottom < MAP_MIN_GAP)
        {
            int raised = Map_Floor(hit->far) - Map_Floor(hit->near) > Map_Ceiling(hit->near) - Map_Ceiling(hit->far);
            ray.distance = travelled;
            ray.surface = side >= 0 ? wall(raised ? sides[side].bottomtexture : sides[side].toptexture) : stone;
            return ray;
        }
        here = hit->far;
    }
    cross_sector(&ray, here, MAP_REACH - travelled);
    return ray;
}

static void dry(room_t *room) { memset(room, 0, sizeof(*room)); room->decay = 0.2f; }

void Room_Measure(room_t *room)
{
    mobj_t *listener = players[consoleplayer].mo;
    if (gamestate != GS_LEVEL || !listener || !listener->subsector || !numlines) { dry(room); return; }
    name_materials();
    float x = listener->x / (float)FRACUNIT, y = listener->y / (float)FRACUNIT;
    sector_t *start = listener->subsector->sector;
    Map_Gather(x, y);

    static ray_t rays[RAYS];
    const float step = 2 * (float)M_PI / RAYS;
    for (int i = 0; i < RAYS; ++i) rays[i] = cast(x, y, cosf(i * step), sinf(i * step), start);

    /* The floor plan the rays outline, and its walls. */
    float area = 0, perimeter = 0, wall_absorb = 0, wall_dark = 0, open = 0;
    ray_t total = {0};
    for (int i = 0; i < RAYS; ++i)
    {
        const ray_t *a = &rays[i], *b = &rays[(i + 1) % RAYS];
        area += 0.5f * a->distance * b->distance * sinf(step);
        float chord = sqrtf(a->distance * a->distance + b->distance * b->distance
                            - 2 * a->distance * b->distance * cosf(step));
        perimeter += chord;
        wall_absorb += chord * 0.5f * (a->surface.absorb + b->surface.absorb);
        wall_dark += chord * 0.5f * (a->surface.dark + b->surface.dark);
        if (a->surface.absorb >= 1) open += 1.0f / RAYS;
        total.length += a->length; total.sky_length += a->sky_length; total.height += a->height;
        total.floor_absorb += a->floor_absorb; total.floor_dark += a->floor_dark;
        total.ceiling_absorb += a->ceiling_absorb; total.ceiling_dark += a->ceiling_dark;
    }
    float length = fmaxf(total.length, 1);
    float height = fmaxf(total.height / length, 16), sky_share = total.sky_length / length;
    float floor_absorb = total.floor_absorb / length, ceiling_absorb = total.ceiling_absorb / length;
    perimeter = fmaxf(perimeter, 1);

    /* Sabine: the tail lasts 0.161 V / A seconds, A the room's absorbing area. */
    float floor_m2 = area * METERS * METERS, height_m = height * METERS, walls_m2 = perimeter * METERS * height_m;
    float volume = floor_m2 * height_m;
    float absorbing = floor_m2 * (floor_absorb + ceiling_absorb) + walls_m2 * wall_absorb / perimeter;
    float sabine = 0.161f * volume / fmaxf(absorbing, 0.01f);
    /* Standing under the sky, most of the sound never comes back. */
    float outdoors = Map_Sky(start) ? 0.4f : 1;
    room->decay = fminf(fmaxf(0.5f * sabine * (Map_Sky(start) ? 0.6f : 1), 0.2f), 2.5f);
    float dark_area = floor_m2 * (total.floor_dark + total.ceiling_dark) / length + walls_m2 * wall_dark / perimeter;
    room->damping = fminf(dark_area / fmaxf(2 * floor_m2 + walls_m2, 0.01f), 1);
    /* Bigger rooms ring louder; sky and open air above and around take it away. */
    float size = fminf(fmaxf((log10f(fmaxf(volume, 1)) - 1.7f) / 2.0f, 0), 1);
    size = size * size * (3 - 2 * size);
    room->late = outdoors * (0.35f + 0.65f * size) * (1 - 0.8f * sky_share) * (1 - 0.6f * open);

    /* An echo off the walls in each eighth of the circle, panned to where it is
       as the listener faces; Doom's angles turn left, so left is +90 degrees. */
    float facing = listener->angle * (2 * (float)M_PI / 4294967296.0f);
    const int per_tap = RAYS / 8;
    for (int k = 0; k < 8; ++k)
    {
        /* The walls that answer set the delay; open air only weakens the echo. */
        float distance = 0, reflect = 0;
        for (int i = 0; i < per_tap; ++i)
        {
            const ray_t *r = &rays[k * per_tap + i];
            distance += r->distance * (1 - r->surface.absorb);
            reflect += (1 - r->surface.absorb) / per_tap;
        }
        distance = reflect > 0 ? distance / (reflect * per_tap) : MAP_REACH;
        float bearing = (k * per_tap + 0.5f * (per_tap - 1)) * step - facing;
        room->delay[k] = 2 * distance / SPEED;
        room->gain[k] = reflect * 0.35f / (1 + distance / 320);
        room->pan[k] = -sinf(bearing);
    }
    /* And one off the ceiling, unless it is sky. */
    float above = Map_Ceiling(start) - (listener->z / (float)FRACUNIT + EAR);
    int ceiling = ROOM_TAPS - 1;
    room->delay[ceiling] = 2 * fmaxf(above, 8) / SPEED;
    room->gain[ceiling] = Map_Sky(start) ? 0 : (1 - flat(start->ceilingpic).absorb) * 0.25f / (1 + fmaxf(above, 8) / 320);
    room->pan[ceiling] = 0;
}
