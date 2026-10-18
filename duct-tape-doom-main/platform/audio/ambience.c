/* How the ambient spots reach the listener, every tic. Every spot of a kind
 * in reach counts, not just the loudest. One that lies in another sector is
 * heard the way the map's openings let it through (acoustic_graph.h): round
 * corners it is farther, through a shut door or a window's glass quieter and
 * duller, and it comes from the opening it arrives by. One in the listener's
 * own sector is heard straight, with a ray through the map now and then for
 * pillars and odd corners. Each spot eases over a few tenths of a second, so
 * sounds hand over smoothly. A kind plays in up to three layers, the spots
 * shared among them, so a room full of computers sounds fuller rather than
 * only louder; the wind is one field all openings and open air add into.
 * Under it all a faint room tone runs. See ambience.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "acoustic_graph.h"
#include "ambience_spots.h"
#include "map.h"
#include "occlusion.h"

#define EAR 41.0f
#define STEADY_LAMP 0.3f   /* a lamp in a steady light only murmurs; a flickering one buzzes */
#define EASE_SECONDS 0.4f  /* how fast a spot's loudness follows the listener */
#define MEASURE_EVERY 6    /* tics between a ray's measurements in the listener's sector */
#define MEASURE_MOVED 16.0f /* a ray waits until the listener moves this far, */
#define MEASURE_DOORS 35   /* or this many tics pass */
#define FADE_DB 30.0f      /* how far a sound falls, evenly in decibels, across its reach */
#define FAR_HZ 1500.0f     /* how dull it has grown at the edge of its reach */
#define THROUGH_WALL 0.25f /* what gets through solid wall when no way round is louder */
#define THROUGH_WALL_WIND 0.1f
#define OPEN_AIR 0.8f      /* the wind's own strength around a listener out under the sky */
#define PI_F 3.14159265f

static float units(fixed_t v) { return v / (float)FRACUNIT; }

/* Falling evenly in decibels with distance, a long quiet tail, only the
   last stretch tapering to nothing. data is the kind's reach_t. */
static float fade(float distance, const void *data)
{
    const reach_t *r = data;
    if (distance >= r->range) return 0;
    float far = distance <= r->near ? 0 : (distance - r->near) / (r->range - r->near);
    return powf(10, -FADE_DB / 20 * far) * (1 - far * far * far * far);
}
/* Far off it grows dull, a sound coming apart rather than switched off. */
static float dulled(float distance, const reach_t *r, int on)
{
    if (!on) return 20000;
    float far = distance <= r->near ? 0 : fminf((distance - r->near) / (r->range - r->near), 1);
    return 20000 * powf(FAR_HZ / 20000, far);
}

/* In the listener's own sector the way is straight; a ray through the map
   finds what stands in it, measured now and then and once they have moved. */
static void ray(spot_t *s, int index, float lx, float ly, int walls, int fresh)
{
    int moved = fabsf(lx - s->measured_x) + fabsf(ly - s->measured_y) > MEASURE_MOVED;
    if (!fresh && ((gametic + index) % MEASURE_EVERY || (!moved && gametic - s->measured < MEASURE_DOORS))) return;
    s->measured_x = lx; s->measured_y = ly; s->measured = gametic;
    float cutoff = Occlusion_Cutoff((fixed_t)(s->x * FRACUNIT), (fixed_t)(s->y * FRACUNIT), (fixed_t)(s->z * FRACUNIT), walls, 0);
    s->muffle_to = cutoff < 1000 ? THROUGH_WALL : cutoff < 2000 ? 0.75f : 1;
    s->cutoff_to = cutoff;
}

void Ambience_Update(ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS], float level, int walls, int distance,
                     ambience_strike_t *strike)
{
    memset(voices, 0, sizeof(ambience_voice_t) * AMBIENT_KINDS * AMBIENCE_LAYERS);
    strike->due = 0;
    const mobj_t *listener = players[consoleplayer].mo;
    if (gamestate != GS_LEVEL || !listener || !listener->subsector || !numsectors) return;
    if (ambience_built != r_levelserial) Ambience_Build();
    float lx = units(listener->x), ly = units(listener->y), lz = units(listener->z) + EAR;
    float facing = listener->angle * (2 * PI_F / 4294967296.0f);
    int here = (int)(listener->subsector->sector - sectors);
    float strike_level = 0, ease = 1 - expf(-1.0f / (EASE_SECONDS * TICRATE));
    if (walls) Acoustic_Follow();
    /* Out under the sky the listener is in the wind, whatever spots lie
       about; eased, so stepping out of a door blows up rather than switches. */
    static float open_air;
    open_air += ((Map_Sky(listener->subsector->sector) ? 1 : 0) - open_air) * ease;

    for (int k = 0; k < AMBIENT_KINDS; ++k)
    {
        if (k == AMBIENT_ROOM) continue;
        const reach_t *r = &ambience_reach[k];
        float sum2[AMBIENCE_LAYERS] = {0}, pan[AMBIENCE_LAYERS] = {0}, dull[AMBIENCE_LAYERS] = {0};
        if (k == AMBIENT_WIND && open_air > 1e-3f)
        {
            float v = OPEN_AIR * open_air;
            sum2[0] = v * v; pan[0] = v * v * 128; dull[0] = v * v * logf(20000);
        }
        for (int i = 0; i < ambience_spots[k].count; ++i)
        {
            spot_t *s = &ambience_spots[k].spot[i];
            float lit = 1, was = s->light;
            int living = 0;
            if (k == AMBIENT_LAMP) { lit = Ambience_LampLight(s, &living); s->light = lit; }
            float dx = s->x - lx, dy = s->y - ly, dz = s->z - lz, d = sqrtf(dx * dx + dy * dy + dz * dz);
            /* No way round is shorter than straight. */
            if (d >= r->range || s->size <= 0) continue;
            int fresh = gametic - s->heard > 2;
            float target, cutoff, fx = s->x, fy = s->y;
            if (!walls) { target = fade(d, r); cutoff = dulled(d, r, distance); }
            else if (s->sector == here)
            {
                ray(s, i, lx, ly, walls, fresh);
                target = fade(d, r) * s->muffle_to;
                cutoff = fminf(s->cutoff_to, dulled(d, r, distance));
            }
            else
            {
                /* The ways round, unless solid wall lets more through. */
                acoustic_heard_t h;
                float wall = fade(d, r) * (k == AMBIENT_WIND ? THROUGH_WALL_WIND : THROUGH_WALL);
                /* However many ways it comes by, never louder than in the open. */
                if (Acoustic_Heard(s->x, s->y, s->sector, fade, r, &h) && h.gain > wall)
                { target = fminf(h.gain, fade(d, r)); cutoff = fminf(h.cutoff, dulled(h.distance, r, distance)); fx = h.from_x; fy = h.from_y; }
                else { target = wall; cutoff = fminf(500, dulled(d, r, distance)); }
            }
            if (fresh) { s->muffle = target; s->cutoff = cutoff; }
            else { s->muffle += (target - s->muffle) * ease; s->cutoff *= powf(cutoff / s->cutoff, ease); }
            s->heard = gametic;
            float v = fminf(sqrtf(s->size / r->full), 1.5f) * s->muffle;
            if (k == AMBIENT_LAMP)
            {
                v *= (living || s->standing ? 1 : STEADY_LAMP) * lit;
                if (living && lit - was > 0.3f && v > strike_level)
                {
                    strike_level = v;
                    *strike = (ambience_strike_t){ 1, s->x, s->y, s->z, 0 };
                }
            }
            if (v < 1e-4f) continue;
            /* Panned as DMX pans, toward where it comes in; centered when it is all around. */
            float ax = fx - lx, ay = fy - ly;
            float sep = 128 - 96 * sinf(atan2f(ay, ax) - facing) * fminf(hypotf(ax, ay) / 64, 1);
            /* The wind is one field the openings add up into, not separate emitters. */
            int l = k == AMBIENT_WIND ? 0 : i % AMBIENCE_LAYERS;
            sum2[l] += v * v; pan[l] += v * v * sep; dull[l] += v * v * logf(s->cutoff);
        }
        /* The cap is on the kind as a whole: more layers make it fuller, not louder. */
        float total2 = 0;
        for (int l = 0; l < AMBIENCE_LAYERS; ++l) total2 += sum2[l];
        float cap = total2 > 1.5f * 1.5f ? 1.5f / sqrtf(total2) : 1;
        for (int l = 0; l < AMBIENCE_LAYERS; ++l)
        {
            if (sum2[l] < 1e-8f) continue;
            float gain = sqrtf(sum2[l]) * cap * Ambient_Level(k) * level, sep = pan[l] / sum2[l];
            voices[k][l] = (ambience_voice_t){ gain * fminf(1, (254 - sep) / 127), gain * fminf(1, sep / 127),
                                               0.8f * gain, expf(dull[l] / sum2[l]) };
        }
    }
    /* The room tone runs all the time, very quietly; whatever else is heard
       covers it, so silence bottoms out instead of a sound waking up. */
    float tone = level * Ambient_Level(AMBIENT_ROOM);
    voices[AMBIENT_ROOM][0] = (ambience_voice_t){ tone, tone, 0.8f * tone, 20000 };
    if (strike->due) strike->level = strike_level * level;
}

void Ambience_Silence(float x, float y, float z, float radius)
{
    static const int fixtures[] = { AMBIENT_LAMP, AMBIENT_CRT, AMBIENT_COMPUTER };
    if (ambience_built != r_levelserial) return;
    for (int f = 0; f < 3; ++f)
        for (int i = 0; i < ambience_spots[fixtures[f]].count; ++i)
        {
            spot_t *s = &ambience_spots[fixtures[f]].spot[i];
            float dx = s->x - x, dy = s->y - y, dz = s->z - z;
            if (dx * dx + dy * dy + dz * dz < radius * radius) s->size = 0;
        }
}

void Ambience_Glaze(int line, int whole) { Acoustic_Glass(line, whole); }
void Ambience_ShowGlass(int shown) { Acoustic_ShowGlass(shown); }
