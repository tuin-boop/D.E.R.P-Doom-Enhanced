/* Performance smoke test: plays through every map of a WAD with the GPU
   renderer, looks around from spots across each map and reports where frames
   get slow and why.

   doom-perf-smoke <iwad> [-maps MAP01,E1M2] [-spacing 512] [-budget 16.7]
                   [-out perf-report] [-strict] [-at x,y[,facing]] [engine options]

   -at times just that spot (facing east, north, west or south, or all four),
   for re-checking a slow view the report found; use it with one map.
   -combat-seconds N runs 1..120 seconds of active plasma combat at that spot.
   Add -combat-realtime to measure actual frame intervals with normal GPU
   pipelining/presentation instead of the profiler's per-frame GPU fence.
   Combat reports median/p95/p99/max intervals and the number over budget;
   the first second is warmup, and fired cells verify that combat ran.
   -door-save path loads a copied native save without moving the player and
   uses the facing door at 2, 12 and 22 seconds. door.csv records normal-render
   intervals and moving/settled geometry over 30 seconds after warmup.

   The world stays paused while the view moves from spot to spot, facing the
   four compass directions at each. Every frame waits for the GPU, so CPU and
   GPU times are measured separately; a pipelined game frame costs about the
   larger of the two. At the slowest view of each map the renderer leaves out
   one kind of work at a time (each kind of dynamic light, reflections, bloom,
   fog, mist), so the report can name what the time goes to, and it saves a
   screenshot of that view. Engine options such as -file, -deh, -skill,
   -width, -height, -fullscreen, -flashlight and -graphics pass through; the
   player's own graphics.cfg is used unless -graphics names another. */
#include "p_notices.h"
extern void D_Display(void);
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "info.h"
#include "m_argv.h"
#include "m_bbox.h"
#include "p_local.h"
#include "r_main.h"
#include "r_sky.h"
#include "r_state.h"
#include "r_data.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "m_menu.h"
extern unsigned p_toasty_serial;
extern int p_toasty_tic;
extern void P_KillMobj(mobj_t*,mobj_t*);
#include "../platform/i_render3d.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_MAPS 160
#define TIMED_FRAMES 5   /* Single GPU frames spike; slow views are timed again. */
#define TOP_SPOTS 8
/* Pitfall thresholds. */
#define MANY_BLOCKERS 12       /* Wall tests per pixel for one light. */
#define MANY_VIEW_BLOCKERS 64  /* ...summed over the lights of one view. */
#define MANY_LIGHTS 16
#define MANY_TRIANGLES 250000  /* The whole map is drawn every frame. */
#define SLOW_LOAD_MS 3000
#define SLOW_MASKS_MS 2.0

typedef struct { int episode, map; char name[9]; } map_t;
typedef struct { fixed_t x, y; int sector; } spot_t;
typedef struct { int spot, yaw; I_Render3DFrameProfile frame; } sample_t;
typedef struct { const char *name; unsigned skip; double gpu, cpu; } ablation_t;
typedef struct { char name[9]; double worst, median, load; int over, spots; } summary_t;

static const char *kind_names[I_RENDER3D_LIGHT_KINDS] = {
    "flashlight", "weapon flashes", "exploding barrels", "decorations (torches, lamps)",
    "projectiles", "door spill", "glowing textures", "fog glow"};
static const char *facing_names[4] = {"east", "north", "west", "south"};

static map_t maps[MAX_MAPS];
static int map_count, map_index = -1;
static int spacing = 512, strict, skill_given;
static int at_x, at_y, at_yaw = -1, at_set; /* -at: one spot, optionally one facing. */
static double budget = 1000.0 / 60;
static const char *out_dir = "perf-report";
static const char *map_filter;
static unsigned shot_skip;
static int combat_seconds,combat_realtime,combat_cells;
static uint64_t combat_start;
static double combat_times[120000],combat_intervals[120000];static int combat_count;
static uint64_t combat_previous;
static const char *door_save;
static int monster_test;
static void out(const char *format, ...);
static void monster_snapshot(int second) {
    mobj_t *player=players[consoleplayer].mo;
    out("MONSTERS second=%d\n",second);
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
        mobj_t *m=(mobj_t*)th;
        if(!(m->flags&MF_COUNTKILL)||m->health<=0||P_AproxDistance(m->x-player->x,m->y-player->y)>1500*FRACUNIT)continue;
        out("actor type=%d xy=%.2f,%.2f z=%d state=%d tics=%d dir=%d count=%d target=%d targetxy=%d,%d radius=%d height=%d flags=%x\n",
            m->type,m->x/(double)FRACUNIT,m->y/(double)FRACUNIT,m->z/FRACUNIT,(int)(m->state-states),m->tics,m->movedir,m->movecount,
            m->target?m->target->type:-1,m->target?m->target->x/FRACUNIT:0,m->target?m->target->y/FRACUNIT:0,m->radius/FRACUNIT,m->height/FRACUNIT,m->flags);
        if(second==0&&m->target==player&&P_AproxDistance(m->x-player->x,m->y-player->y)<512*FRACUNIT) {
            extern fixed_t tmfloorz,tmceilingz,tmdropoffz;
            out("awake sight=%d angle=%u health=%d spawn=%d see=%d\n",P_CheckSight(m,player),m->angle,m->health,m->info->spawnstate,m->info->seestate);
            out("collision sector=%d floor=%d ceiling=%d\n",(int)(m->subsector->sector-sectors),m->floorz/FRACUNIT,m->ceilingz/FRACUNIT);
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                int ok=P_CheckPosition(m,m->x+x*8*FRACUNIT,m->y+y*8*FRACUNIT);
                out("offset=%d,%d ok=%d floor=%d ceiling=%d drop=%d\n",x*8,y*8,ok,tmfloorz/FRACUNIT,tmceilingz/FRACUNIT,tmdropoffz/FRACUNIT);
            }
            for(int l=0;l<numlines;++l){
                line_t *line=&lines[l];
                if(m->x+32*FRACUNIT<line->bbox[BOXLEFT]||m->x-32*FRACUNIT>line->bbox[BOXRIGHT]||
                   m->y+32*FRACUNIT<line->bbox[BOXBOTTOM]||m->y-32*FRACUNIT>line->bbox[BOXTOP])continue;
                out("near line=%d flags=%x special=%d endpoints=%d,%d:%d,%d floors=%d,%d\n",l,line->flags,line->special,
                    line->v1->x/FRACUNIT,line->v1->y/FRACUNIT,line->v2->x/FRACUNIT,line->v2->y/FRACUNIT,
                    line->frontsector?line->frontsector->floorheight/FRACUNIT:-9999,
                    line->backsector?line->backsector->floorheight/FRACUNIT:-9999);
            }
        }
    }
}
static fixed_t *door_heights;
static int door_action,door_changes,door_moving_tic=-10;
extern int key_fire;

static FILE *report, *csv;
static summary_t summaries[MAX_MAPS];

static enum { START, LOADING, SETTLING, SAMPLING, CONFIRMING, ABLATING, SHOT_PREP, SHOT, COMBAT, DOOR, FINISHED } state = START;
static unsigned seen_serial, level_serial;
static uint64_t request_time;
static double load_ms;
static int settle_frames;
static spot_t *spots;
static int spot_count;
static sample_t *samples;
static int sample_count;
static int view;              /* SAMPLING: spot * 5 + phase; phase 0 settles. */
static int candidates[TOP_SPOTS], candidate_count, candidate; /* The slowest view of the slowest spots. */
static ablation_t ablations[16];
static int ablation_count, ablation;
static double timed_gpu[TIMED_FRAMES], timed_cpu[TIMED_FRAMES];
static int timed_frame = -1;
static int worst;             /* Index into samples. */
static char shot_path[512];

static void out(const char *format, ...)
{
    va_list args;
    va_start(args, format); vprintf(format, args); va_end(args);
    if (report) { va_start(args, format); vfprintf(report, format, args); va_end(args); }
}
static double cost(const I_Render3DFrameProfile *f) { return f->cpuMs > f->gpuMs ? f->cpuMs : f->gpuMs; }
static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static double median(double *values, int count)
{
    qsort(values, count, sizeof(double), compare_doubles);
    return values[count / 2];
}
/* Times the view over TIMED_FRAMES frames after one that settles; true
   with the medians once done. */
static int time_view(const I_Render3DFrameProfile *frame, double *gpu, double *cpu)
{
    if (timed_frame >= 0) { timed_gpu[timed_frame] = frame->gpuMs; timed_cpu[timed_frame] = frame->cpuMs; }
    if (++timed_frame < TIMED_FRAMES) return 0;
    *gpu = median(timed_gpu, TIMED_FRAMES); *cpu = median(timed_cpu, TIMED_FRAMES);
    timed_frame = -1;
    return 1;
}
static const char *thing_name(int type, char *buffer, size_t size)
{
    if (type < 0 || type >= NUMMOBJTYPES) return "";
    const char *sprite = sprnames[states[mobjinfo[type].spawnstate].sprite];
    snprintf(buffer, size, " %.4s #%d", sprite ? sprite : "????", mobjinfo[type].doomednum);
    return buffer;
}

static void find_maps(void)
{
    char wanted[512] = "";
    if (map_filter)
    {
        snprintf(wanted, sizeof(wanted), ",%s,", map_filter);
        for (char *c = wanted; *c; ++c) *c = (char)toupper((unsigned char)*c);
    }
    for (int e = 1; e <= (gamemode == commercial ? 1 : 5); ++e)
        for (int m = 1; m <= (gamemode == commercial ? 32 : 9); ++m)
        {
            char name[9], key[12];
            if (gamemode == commercial) snprintf(name, sizeof(name), "MAP%02d", m);
            else snprintf(name, sizeof(name), "E%dM%d", e, m);
            snprintf(key, sizeof(key), ",%s,", name);
            if (W_CheckNumForName(name) < 0 || (map_filter && !strstr(wanted, key))) continue;
            if (map_count == MAX_MAPS) break;
            maps[map_count].episode = e; maps[map_count].map = m;
            strcpy(maps[map_count++].name, name);
        }
    if (!map_count) I_Error("Performance smoke: no maps to test");
}

/* Whether the player could stand there, clear of walls and solid things. */
static boolean fits(fixed_t x, fixed_t y)
{
    mobj_t *mo = players[consoleplayer].mo;
    int flags = mo->flags;
    mo->flags &= ~MF_PICKUP; /* Checking must not collect items. */
    boolean clear = P_CheckPosition(mo, x, y);
    mo->flags = flags;
    return clear;
}

/* One spot per grid cell: the subsector center nearest the cell's middle
   where the player fits. */
static void find_spots(void)
{
    fixed_t low[2] = {INT32_MAX, INT32_MAX}, high[2] = {INT32_MIN, INT32_MIN};
    for (int i = 0; i < numvertexes; ++i)
    {
        if (vertexes[i].x < low[0]) low[0] = vertexes[i].x;
        if (vertexes[i].y < low[1]) low[1] = vertexes[i].y;
        if (vertexes[i].x > high[0]) high[0] = vertexes[i].x;
        if (vertexes[i].y > high[1]) high[1] = vertexes[i].y;
    }
    int columns = (int)(((int64_t)high[0] - low[0]) / FRACUNIT / spacing) + 1;
    int rows = (int)(((int64_t)high[1] - low[1]) / FRACUNIT / spacing) + 1;
    double *nearest = malloc(sizeof(double) * columns * rows);
    free(spots);
    spots = malloc(sizeof(spot_t) * columns * rows);
    if (!nearest || !spots) I_Error("Performance smoke: out of memory");
    for (int i = 0; i < columns * rows; ++i) nearest[i] = -1;
    for (int i = 0; i < numsubsectors; ++i)
    {
        const sector_t *sector = subsectors[i].sector;
        if (sector->ceilingheight - sector->floorheight < 56 * FRACUNIT || sector->floorpic == skyflatnum) continue;
        double x = 0, y = 0;
        int count = subsectors[i].numlines;
        if (count < 1) continue;
        for (int n = 0; n < count; ++n)
        {
            const seg_t *seg = &segs[subsectors[i].firstline + n];
            x += (double)seg->v1->x + seg->v2->x; y += (double)seg->v1->y + seg->v2->y;
        }
        fixed_t px = (fixed_t)(x / (2 * count)), py = (fixed_t)(y / (2 * count));
        if (R_PointInSubsector(px, py) != &subsectors[i] || !fits(px, py)) continue;
        double cx = ((double)px - low[0]) / FRACUNIT / spacing, cy = ((double)py - low[1]) / FRACUNIT / spacing;
        int cell = (int)cy * columns + (int)cx;
        double distance = hypot(cx - floor(cx) - 0.5, cy - floor(cy) - 0.5);
        if (nearest[cell] < 0 || distance < nearest[cell])
        {
            nearest[cell] = distance;
            spots[cell].x = px; spots[cell].y = py; spots[cell].sector = (int)(sector - sectors);
        }
    }
    spot_count = 0;
    for (int i = 0; i < columns * rows; ++i)
        if (nearest[i] >= 0) spots[spot_count++] = spots[i];
    if (at_set)
    {
        spots[0].x = at_x * FRACUNIT; spots[0].y = at_y * FRACUNIT;
        spots[0].sector = (int)(R_PointInSubsector(spots[0].x, spots[0].y)->sector - sectors);
        spot_count = 1;
    }
    free(nearest);
    free(samples);
    samples = malloc(sizeof(sample_t) * (spot_count * 4 + 1));
    if (!samples) I_Error("Performance smoke: out of memory");
    sample_count = 0;
}

static void place(const spot_t *spot, int yaw)
{
    player_t *player = &players[consoleplayer];
    mobj_t *mo = player->mo;
    P_UnsetThingPosition(mo);
    mo->x = spot->x; mo->y = spot->y;
    P_SetThingPosition(mo);
    mo->floorz = mo->subsector->sector->floorheight;
    mo->ceilingz = mo->subsector->sector->ceilingheight;
    mo->z = mo->floorz; mo->momx = mo->momy = mo->momz = 0;
    if (at_yaw >= 0) yaw = at_yaw;
    mo->angle = (angle_t)yaw * ANG90;
    player->viewheight = VIEWHEIGHT; player->deltaviewheight = 0;
    player->viewz = mo->z + VIEWHEIGHT;
    if (player->viewz > mo->ceilingz - 4 * FRACUNIT) player->viewz = mo->ceilingz - 4 * FRACUNIT;
}

static void request_map(void)
{
    const map_t *map = &maps[map_index];
    printf("Loading %s (%d of %d)...\n", map->name, map_index + 1, map_count);
    fflush(stdout);
    level_serial = r_levelserial;
    request_time = SDL_GetTicksNS();
    G_DeferedInitNew(skill_given ? gameskill : sk_hard, map->episode, map->map);
    state = LOADING;
}

static void record(int spot, int yaw, const I_Render3DFrameProfile *frame)
{
    sample_t *sample = &samples[sample_count++];
    sample->spot = spot; sample->yaw = yaw; sample->frame = *frame;
    int most = 0;
    for (int i = 0; i < frame->lights; ++i)
        if (frame->light[i].blockers > most) most = frame->light[i].blockers;
    const spot_t *s = &spots[spot];
    fprintf(csv, "%s,%d,%d,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d\n",
            maps[map_index].name, spot, s->x >> FRACBITS, s->y >> FRACBITS, s->sector, facing_names[yaw],
            cost(frame), frame->cpuMs, frame->gpuMs, frame->prepareMs, frame->geometryMs, frame->lightsMs,
            frame->spritesMs, frame->masksMs, frame->lights, frame->blockers, most, frame->triangles,
            frame->litTriangles, frame->lightTriangles, frame->reflection);
}

/* The slowest view of each of the slowest spots, by their single frames. */
static void pick_candidates(void)
{
    candidate_count = 0;
    while (candidate_count < TOP_SPOTS)
    {
        int best = -1;
        for (int i = 0; i < sample_count; ++i)
        {
            int taken = 0;
            for (int n = 0; n < candidate_count; ++n) taken |= samples[i].spot == samples[candidates[n]].spot;
            if (!taken && (best < 0 || cost(&samples[i].frame) > cost(&samples[best].frame))) best = i;
        }
        if (best < 0) break;
        candidates[candidate_count++] = best;
    }
    candidate = 0;
}

static void plan_ablations(void)
{
    const I_Render3DFrameProfile *f = &samples[worst].frame;
    unsigned kinds = 0;
    for (int i = 0; i < f->lights; ++i) kinds |= 1u << f->light[i].kind;
    ablation_count = 0;
    ablations[ablation_count++] = (ablation_t){"(as played)", 0};
    if (kinds) ablations[ablation_count++] = (ablation_t){"all dynamic lights", I_RENDER3D_SKIP_LIGHTS};
    if (kinds & (kinds - 1))
        for (int k = 0; k < I_RENDER3D_LIGHT_KINDS; ++k)
            if (kinds & (1u << k)) ablations[ablation_count++] = (ablation_t){kind_names[k], 1u << k};
    if (f->reflection) ablations[ablation_count++] = (ablation_t){"liquid reflections", I_RENDER3D_SKIP_REFLECTIONS};
    ablations[ablation_count++] = (ablation_t){"bloom", I_RENDER3D_SKIP_BLOOM};
    ablations[ablation_count++] = (ablation_t){"fog", I_RENDER3D_SKIP_FOG};
    ablations[ablation_count++] = (ablation_t){"mist and sunbeams", I_RENDER3D_SKIP_MIST};
    ablation = 0;
    I_Render3DProfile(1, ablations[0].skip);
}

static void describe_lights(const I_Render3DFrameProfile *f, const char *indent)
{
    int done[64] = {0};
    for (int i = 0; i < f->lights; ++i)
    {
        if (done[i]) continue;
        int count = 0, least = INT32_MAX, most = 0, triangles = 0;
        float radius = 0, per_pixel = 0;
        for (int j = i; j < f->lights; ++j)
        {
            const I_Render3DLightProfile *l = &f->light[j];
            if (done[j] || l->kind != f->light[i].kind || l->thing != f->light[i].thing) continue;
            done[j] = 1; ++count; triangles += l->triangles;
            if (l->blockers < least) least = l->blockers;
            if (l->blockers > most) most = l->blockers;
            if (l->radius > radius) radius = l->radius;
            if (l->perPixel > per_pixel) per_pixel = l->perPixel;
        }
        char name[32];
        const char *kind = f->light[i].kind < I_RENDER3D_LIGHT_KINDS ? kind_names[f->light[i].kind] : "?";
        if (least == most)
            out("%s%2d x %s%s: radius %.0f, %d wall blockers each (up to %.1f per pixel), %d triangles in reach\n", indent, count, kind,
                thing_name(f->light[i].thing, name, sizeof(name)), radius, most, per_pixel, triangles);
        else
            out("%s%2d x %s%s: radius up to %.0f, %d-%d wall blockers each (up to %.1f per pixel), %d triangles in reach\n", indent, count, kind,
                thing_name(f->light[i].thing, name, sizeof(name)), radius, least, most, per_pixel, triangles);
    }
}

static void spot_line(int sample, const char *indent)
{
    const sample_t *s = &samples[sample];
    const spot_t *spot = &spots[s->spot];
    const I_Render3DFrameProfile *f = &s->frame;
    out("%s%6.1f ms (CPU %.1f, GPU %.1f) at (%d, %d) sector %d facing %s: %d lights, %d blockers\n", indent,
        cost(f), f->cpuMs, f->gpuMs, spot->x >> FRACBITS, spot->y >> FRACBITS, spot->sector,
        facing_names[s->yaw], f->lights, f->blockers);
}

static void report_map(void)
{
    const map_t *map = &maps[map_index];
    summary_t *summary = &summaries[map_index];
    strcpy(summary->name, map->name);
    summary->load = load_ms; summary->spots = spot_count;
    int things = 0;
    for (int i = 0; i < numsectors; ++i)
        for (mobj_t *thing = sectors[i].thinglist; thing; thing = thing->snext) ++things;
    out("\n== %s: %d spots, %d lines, %d sectors, %d things, loaded and baked in %.1f s\n",
        map->name, spot_count, numlines, numsectors, things, load_ms / 1000);
    if (!sample_count) { out("   No spots with room for the player.\n"); return; }
    double *costs = malloc(sizeof(double) * sample_count);
    for (int i = 0; i < sample_count; ++i)
    {
        costs[i] = cost(&samples[i].frame);
        if (costs[i] > budget) ++summary->over;
    }
    qsort(costs, sample_count, sizeof(double), compare_doubles);
    summary->median = costs[sample_count / 2];
    summary->worst = cost(&samples[worst].frame);
    out("   One frame per view: median %.1f ms, 95%% %.1f ms; %d of %d views over the %.1f ms budget\n",
        summary->median, costs[(int)(sample_count * 0.95)], summary->over, sample_count, budget);
    free(costs);

    const I_Render3DFrameProfile *f = &samples[worst].frame;
    out("   Slowest view (median of %d frames), %s-bound:\n", TIMED_FRAMES, f->cpuMs > f->gpuMs ? "CPU" : "GPU");
    spot_line(worst, "     ");
    out("     CPU: scene %.1f ms (geometry %.1f: light wall blockers %.1f, sprite lighting %.1f; light masks %.1f)\n",
        f->prepareMs, f->geometryMs, f->lightsMs, f->spritesMs, f->masksMs);
    out("     %d world triangles, %d lit by dynamic lights (%d light-triangle pairs)%s\n", f->triangles,
        f->litTriangles, f->lightTriangles, f->reflection ? ", liquid reflection pass" : "");
    if (f->lights) { out("     Lights:\n"); describe_lights(f, "       "); }
    out("     Saved by leaving out (GPU / CPU, median of %d frames):\n", TIMED_FRAMES);
    double base_gpu = ablations[0].gpu, base_cpu = ablations[0].cpu, light_saving = 0;
    out("       %-30s %6.1f / %5.1f ms\n", ablations[0].name, base_gpu, base_cpu);
    for (int i = 1; i < ablation_count; ++i)
    {
        int nested = ablations[i].skip && ablations[i].skip < I_RENDER3D_SKIP_LIGHTS;
        out("       %s%-*s %+6.1f / %+5.1f ms\n", nested ? "  " : "", nested ? 28 : 30, ablations[i].name,
            ablations[i].gpu - base_gpu, ablations[i].cpu - base_cpu);
        if (ablations[i].skip == I_RENDER3D_SKIP_LIGHTS) light_saving = base_gpu - ablations[i].gpu;
    }
    out("     Screenshot: %s\n", shot_path);

    /* Pitfalls over the whole map. */
    int warnings = 0;
#define WARN(...) do { out(warnings++ ? "     ! " : "   Pitfalls:\n     ! "); out(__VA_ARGS__); } while (0)
    if (summary->worst > budget)
        WARN("over budget: %.1f ms at the slowest view\n", summary->worst);
    if (light_saving > 2 && light_saving > 0.4 * base_gpu)
        WARN("dynamic lights take %.0f%% of the GPU frame there (%.1f of %.1f ms)\n", 100 * light_saving / base_gpu, light_saving, base_gpu);
    for (int i = 1; i < ablation_count; ++i)
        if (!(ablations[i].skip & I_RENDER3D_SKIP_LIGHTS) && base_gpu - ablations[i].gpu > 0.25 * base_gpu && base_gpu - ablations[i].gpu > 2)
            WARN("%s cost %.1f ms of GPU there\n", ablations[i].name, base_gpu - ablations[i].gpu);
    if (f->cpuMs > budget)
        WARN("CPU-bound: building the frame takes %.1f ms (%s)\n", f->cpuMs,
             f->spritesMs > f->lightsMs && f->spritesMs > f->masksMs ? "mostly sprite lighting: things x lights x blockers" :
             f->lightsMs > f->masksMs ? "mostly finding the wall blockers of each light" : "mostly light masks: triangles x lights");
    if (f->masksMs > SLOW_MASKS_MS) WARN("light masks take %.1f ms on the CPU (%d triangles x %d lights)\n", f->masksMs, f->triangles, f->lights);
    if (f->triangles > MANY_TRIANGLES) WARN("%d world triangles: the whole map is drawn every frame\n", f->triangles);
    if (load_ms > SLOW_LOAD_MS) WARN("level load and bakes take %.1f s\n", load_ms / 1000);
    int busiest = -1, crowded = -1, most_lights = 0, heavy = -1, heavy_light = 0;
    float most = 0;
    for (int i = 0; i < sample_count; ++i)
    {
        const I_Render3DFrameProfile *s = &samples[i].frame;
        float per_pixel = 0;
        for (int n = 0; n < s->lights; ++n) per_pixel += s->light[n].perPixel;
        if (per_pixel > most) { most = per_pixel; busiest = i; }
        if (s->lights > most_lights) { most_lights = s->lights; crowded = i; }
        for (int n = 0; n < s->lights; ++n)
            if (s->light[n].perPixel > (heavy < 0 ? 0 : samples[heavy].frame.light[heavy_light].perPixel)) { heavy = i; heavy_light = n; }
    }
    if (heavy >= 0 && samples[heavy].frame.light[heavy_light].perPixel >= MANY_BLOCKERS)
    {
        const I_Render3DLightProfile *l = &samples[heavy].frame.light[heavy_light];
        char name[32];
        WARN("a %s%s light at (%.0f, %.0f) tests %.1f of its %d wall blockers in every pixel it reaches (radius %.0f)\n",
             kind_names[l->kind], thing_name(l->thing, name, sizeof(name)), l->x, l->y, l->perPixel, l->blockers, l->radius);
    }
    if (busiest >= 0 && most >= MANY_VIEW_BLOCKERS)
    {
        WARN("a pixel lit by all the lights of one view tests %.0f wall blockers:\n", most);
        spot_line(busiest, "         ");
        describe_lights(&samples[busiest].frame, "         ");
    }
    if (crowded >= 0 && most_lights >= MANY_LIGHTS && samples[crowded].spot != samples[worst].spot &&
        (busiest < 0 || samples[crowded].spot != samples[busiest].spot))
    {
        WARN("%d dynamic lights at once:\n", most_lights);
        spot_line(crowded, "         ");
    }
    if (!warnings) out("   No pitfalls found.\n");
#undef WARN

    /* The other timed spots, slowest first. */
    if (candidate_count > 1) out("   Other slow spots (median of %d frames):\n", TIMED_FRAMES);
    for (int i = 0; i < candidate_count; ++i)
        for (int j = i + 1; j < candidate_count; ++j)
            if (cost(&samples[candidates[j]].frame) > cost(&samples[candidates[i]].frame))
            { int t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t; }
    for (int i = 0; i < candidate_count; ++i)
        if (candidates[i] != worst) spot_line(candidates[i], "     ");
    fflush(report); fflush(csv);
}

static void report_summary(void)
{
    int over = 0;
    out("\n== Summary, slowest first (budget %.1f ms)\n   Map       Worst  Median  Views over  Load\n", budget);
    int order[MAX_MAPS];
    for (int i = 0; i < map_count; ++i) order[i] = i;
    for (int i = 0; i < map_count; ++i)
        for (int j = i + 1; j < map_count; ++j)
            if (summaries[order[j]].worst > summaries[order[i]].worst) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < map_count; ++i)
    {
        const summary_t *s = &summaries[order[i]];
        out("   %-8s %6.1f  %6.1f  %10d  %4.1f s%s\n", s->name, s->worst, s->median, s->over, s->load / 1000,
            s->worst > budget ? "  !" : "");
        over += s->worst > budget;
    }
    out("\n%d of %d maps have views over budget. Report: %s/report.txt, every view: %s/views.csv\n",
        over, map_count, out_dir, out_dir);
    fclose(report); fclose(csv); report = csv = NULL;
    if (strict && over) I_Error("Performance smoke: %d maps over budget", over);
}

void I_TestFrame(void)
{
    if (state == FINISHED) return;
    const I_Render3DFrameProfile *frame = I_Render3DLastFrame();
    int fresh = frame->serial != seen_serial && frame->world && frame->gpuMs > 0;
    if(state==COMBAT&&combat_realtime)fresh=1;
    seen_serial = frame->serial;
    switch (state)
    {
    case START:
        if (gamestate != GS_LEVEL || !players[consoleplayer].mo) return;
        if (!I_Render3DIsAccelerated()) I_Error("Performance smoke: the GPU renderer is off");
        {
            boolean saved=debugnotarget;debugnotarget=false;
            const char *code="notarget";
            for(int pass=0;pass<2;++pass) {
                for(const char *c=code;*c;++c) {
                    event_t key={ev_keydown,*c,0,0};ST_Responder(&key);
                }
                if(debugnotarget!=(pass==0)||P_Unseen(players[consoleplayer].mo)!=(pass==0))
                    I_Error("Notarget typed cheat did not toggle player visibility");
            }
            debugnotarget=saved;
            fprintf(stderr,"Notarget regression: typed ON/OFF and player visibility passed.\n");
        }
        {
            extern int S_AdjustSoundParams(mobj_t*,mobj_t*,int*,int*,int*);
            extern int snd_SfxVolume;
            mobj_t listener={0},source={0};
            int savedMap=gamemap,savedVolume=snd_SfxVolume,sep=128,pitch=128;
            snd_SfxVolume=15;
            for(int distance=0;distance<=1600;distance+=80) {
                int regular=15,eighth=15;
                source.x=distance*FRACUNIT;
                gamemap=1;int a=S_AdjustSoundParams(&listener,&source,&regular,&sep,&pitch);
                gamemap=8;int b=S_AdjustSoundParams(&listener,&source,&eighth,&sep,&pitch);
                if(a!=b||(a&&regular!=eighth)||(distance>=1200&&b))
                    I_Error("Positional sound falloff differs on map 8 at %d",distance);
                if(distance==640&&(!b||eighth>=15||eighth<=0))
                    I_Error("Distant sound does not attenuate");
            }
            gamemap=savedMap;snd_SfxVolume=savedVolume;
            fprintf(stderr,"Positional sound regression: maps 1/8 agree at 21 distances.\n");
        }
        find_maps();
        SDL_CreateDirectory(out_dir);
        char path[512];
        snprintf(path, sizeof(path), "%s/report.txt", out_dir);
        report = fopen(path, "w");
        snprintf(path, sizeof(path), "%s/views.csv", out_dir);
        csv = fopen(path, "w");
        if (!report || !csv) I_Error("Performance smoke: cannot write to %s", out_dir);
        fputs("map,spot,x,y,sector,facing,frame_ms,cpu_ms,gpu_ms,scene_ms,geometry_ms,light_blockers_ms,"
              "sprite_light_ms,light_masks_ms,lights,blockers,most_light_blockers,triangles,lit_triangles,"
              "light_triangles,reflection\n", csv);
        {
            int count = 0, w = 0, h = 0, fullscreen = 0;
            SDL_Window **windows = SDL_GetWindows(&count);
            if (windows && count) {SDL_GetWindowSizeInPixels(windows[0], &w, &h);fullscreen=(SDL_GetWindowFlags(windows[0])&SDL_WINDOW_FULLSCREEN)!=0;}
            SDL_free(windows);
            int p = M_CheckParm("-graphics");
            out("DOOM performance smoke test: %d maps, window %dx%d pixels, spots every %d units, budget %.1f ms\n",
                map_count, w, h, spacing, budget);
            out("Presentation: %s\n",fullscreen?"fullscreen":"windowed");
            out("Graphics settings: %s\n", p && p + 1 < myargc ? myargv[p + 1] : "defaults (no graphics.cfg)");
            out("Each frame waits for the GPU, so CPU and GPU times are separate; a played frame costs about the larger.\n");
        }
        if(combat_seconds)out("Live combat stress: %d seconds, active monsters, god mode, held plasma fire.\n",combat_seconds);
        I_Render3DProfile(1, 0);
        map_index = 0;
        if(door_save) {
            level_serial=r_levelserial;request_time=SDL_GetTicksNS();
            G_LoadGame((char*)door_save);state=LOADING;return;
        }
        request_map();
        return;
    case LOADING:
        if (gameaction != ga_nothing || gamestate != GS_LEVEL || r_levelserial == level_serial || !players[consoleplayer].mo)
            return;
        paused = true; /* G_InitNew unpauses. */
        settle_frames = 0;
        state = SETTLING;
        return;
    case SETTLING:
        /* The first frames bake the level's light. */
        if (fresh && ++settle_frames == 3)
        {
            load_ms = (SDL_GetTicksNS() - request_time) / 1e6;
            if(M_CheckParm("-test-pickup-lights")) {
                int sprites[]={SPR_ARM1,SPR_ARM2,SPR_SOUL,SPR_MEGA};
                mobj_t* player=players[consoleplayer].mo;
                for(int n=0;n<4;++n)for(int type=0;type<NUMMOBJTYPES;++type)
                    if(states[mobjinfo[type].spawnstate].sprite==sprites[n]){
                        P_SpawnMobj(player->x+(n*80-120)*FRACUNIT,player->y+96*FRACUNIT,ONFLOORZ,type);break;
                    }
                out("Pickup light fixture: green armor, blue armor, soul and megasphere.\n");
            }
            if(M_CheckParm("-test-special-notices")) {
                int arg=M_CheckParm("-test-special-notices");int first=arg+1<myargc?atoi(myargv[arg+1]):0;
                for(int n=0;n<5&&first+n<N_COUNT;++n)P_Notice(consoleplayer,first+n,wp_bfg);
                D_Display();
                out("Special notice rendering fixture, first event %d.\n",first);
            }
            if(M_CheckParm("-test-completion-notices")) {
                extern unsigned p_keypickup_serial[MAXPLAYERS];
                extern int p_keypickup_card[MAXPLAYERS],p_keypickup_tic[MAXPLAYERS];
                players[consoleplayer].killcount=totalkills;
                players[consoleplayer].secretcount=totalsecret;
                players[consoleplayer].itemcount=totalitems;
                p_keypickup_card[consoleplayer]=0;p_keypickup_tic[consoleplayer]=leveltime;
                ++p_keypickup_serial[consoleplayer];
                // Render once to observe awards, then advance their presentation.
                D_Display();
                out("Completion notice fixture: all counters completed alongside a blue key.\n");
            }
            if(M_CheckParm("-test-toasty-menu")) {
                unsigned before=p_toasty_serial;
                event_t open={ev_keydown,KEY_F5,0,0},select={ev_keydown,'s',0,0},activate={ev_keydown,KEY_ENTER,0,0};
                M_Responder(&open);
                if(!menuactive)I_Error("F5 did not open cheat menu");
                M_Responder(&select);M_Responder(&activate);
                if(menuactive||p_toasty_serial!=before+1)I_Error("F5 Toasty test did not trigger and close menu");
                p_toasty_tic=leveltime-12;
                out("F5 menu: Test Toasty selected, triggered, and returned to gameplay.\n");
            }
            if(M_CheckParm("-test-toasty")) {
                mobj_t *player=players[consoleplayer].mo;
                unsigned before=p_toasty_serial;
                for(int i=0;i<100;++i) {
                    mobj_t *monster=P_SpawnMobj(player->x,player->y,ONFLOORZ,MT_TROOP);
                    monster->health=0;P_KillMobj(player,monster);P_RemoveMobj(monster);
                }
                if(p_toasty_serial!=before)I_Error("Normal deaths triggered Toasty");
                for(int i=0;i<1000;++i) {
                    mobj_t *monster=P_SpawnMobj(player->x,player->y,ONFLOORZ,MT_TROOP);
                    monster->health=-10000;P_KillMobj(player,monster);P_RemoveMobj(monster);
                }
                unsigned hits=p_toasty_serial-before;
                if(hits<70||hits>130)I_Error("Unexpected Toasty rate: %u/1000",hits);
                p_toasty_tic=leveltime-12;
                int age=M_CheckParm("-toasty-age");
                if(age&&age+1<myargc)p_toasty_tic=leveltime-atoi(myargv[age+1]);
                out("Toasty regression: 100 normal deaths silent; %u/1000 gib deaths triggered.\n",hits);
            }
            if(M_CheckParm("-verify-switch-gallery")) {
                int checked=0;
                for(int i=0;i<numlines;++i)if(lines[i].special==138&&lines[i].tag==32000) {
                    side_t *side=&sides[lines[i].sidenum[0]];int before=side->toptexture;
                    P_UseSpecialLine(players[consoleplayer].mo,&lines[i],0);
                    if(side->toptexture==before)I_Error("Gallery switch %d did not change",i);
                    for(int tic=0;tic<TICRATE;++tic)P_UpdateSpecials();
                    if(side->toptexture!=before)I_Error("Gallery switch %d did not reset",i);
                    ++checked;
                }
                if(!checked)I_Error("No gallery switches found");
                out("Verified %d usable gallery switches and timed resets.\n",checked);
            }
            if(M_CheckParm("-test-switch-toggle")) {
                int changed=0;
                for(int i=0;i<numsides;++i) {
                    short *slots[]={&sides[i].toptexture,&sides[i].midtexture,&sides[i].bottomtexture};
                    for(int s=0;s<3;++s) {
                        char name[9];R_TextureName(*slots[s],name);
                        if(name[0]!='S'||name[1]!='W'||(name[2]!='1'&&name[2]!='2'))continue;
                        name[2]=name[2]=='1'?'2':'1';int other=R_CheckTextureNumForName(name);
                        if(other>=0){*slots[s]=other;++changed;}
                    }
                }
                if(!changed)I_Error("Switch test found no switch textures");
                out("Changed %d switch textures after the initial lighting bake.\n",changed);
            }
            if(door_save) {
                if(monster_test){monster_snapshot(0);I_Render3DScreenshot();}
                fclose(csv);char path[512];snprintf(path,sizeof(path),"%s/door.csv",out_dir);csv=fopen(path,"w");
                if(!csv)I_Error("Cannot write door timings");
                fputs("seconds,frame_ms,moving\n",csv);
                door_heights=calloc((size_t)numsectors*2,sizeof(fixed_t));
                if(!door_heights)I_Error("Cannot allocate door test heights");
                for(int i=0;i<numsectors;++i){door_heights[i*2]=sectors[i].floorheight;door_heights[i*2+1]=sectors[i].ceilingheight;}
                out("Saved door: E%dM%d, x=%d y=%d angle=%u. Use at 2,12,22 seconds; measure 30 seconds.\n",gameepisode,gamemap,players[consoleplayer].mo->x/FRACUNIT,players[consoleplayer].mo->y/FRACUNIT,players[consoleplayer].mo->angle);
                players[consoleplayer].cheats|=CF_GODMODE;
                paused=false;combat_start=SDL_GetTicksNS();combat_previous=0;state=DOOR;I_Render3DProfile(0,0);return;
            }
            find_spots();
            if (!spot_count) { worst = 0; report_map(); goto next_map; }
            if(combat_seconds) {
                place(&spots[0],0);
                player_t *p=&players[consoleplayer];p->cheats|=CF_GODMODE;
                p->weaponowned[wp_plasma]=true;p->readyweapon=wp_plasma;p->pendingweapon=wp_nochange;
                p->ammo[am_cell]=9999;P_SetupPsprites(p);
                event_t fire={ev_keydown,key_fire,0,0};D_PostEvent(&fire);
                paused=false;combat_start=SDL_GetTicksNS();combat_count=0;combat_previous=0;combat_cells=0;state=COMBAT;
                if(combat_realtime)I_Render3DProfile(0,0);
                return;
            }
            view = 0;
            state = SAMPLING;
            I_Render3DProfileTeleported();
            place(&spots[0], 0);
        }
        return;
    case SAMPLING:
        if (!fresh) { place(&spots[view / 5], view % 5 ? view % 5 - 1 : 0); return; }
        if (view % 5) record(view / 5, view % 5 - 1, frame);
        if (++view == spot_count * 5)
        {
            pick_candidates();
            state = CONFIRMING;
            I_Render3DProfileTeleported();
            place(&spots[samples[candidates[0]].spot], samples[candidates[0]].yaw);
            printf("  %d spots, %d views; timing the slowest again\n", spot_count, sample_count);
            fflush(stdout);
            return;
        }
        if (view % 5 == 0)
        {
            I_Render3DProfileTeleported();
            if ((view / 5) % 20 == 0) { printf("  %d of %d spots\r", view / 5, spot_count); fflush(stdout); }
        }
        place(&spots[view / 5], view % 5 ? view % 5 - 1 : 0);
        return;
    case CONFIRMING:
    {
        sample_t *sample = &samples[candidates[candidate]];
        place(&spots[sample->spot], sample->yaw);
        if (!fresh || !time_view(frame, &sample->frame.gpuMs, &sample->frame.cpuMs)) return;
        if (++candidate < candidate_count)
        {
            I_Render3DProfileTeleported();
            place(&spots[samples[candidates[candidate]].spot], samples[candidates[candidate]].yaw);
            return;
        }
        worst = candidates[0];
        for (int i = 1; i < candidate_count; ++i)
            if (cost(&samples[candidates[i]].frame) > cost(&samples[worst].frame)) worst = candidates[i];
        plan_ablations();
        state = ABLATING;
        I_Render3DProfileTeleported();
        place(&spots[samples[worst].spot], samples[worst].yaw);
        return;
    }
    case ABLATING:
        place(&spots[samples[worst].spot], samples[worst].yaw);
        if (!fresh || !time_view(frame, &ablations[ablation].gpu, &ablations[ablation].cpu)) return;
        if (++ablation < ablation_count) { I_Render3DProfile(1, ablations[ablation].skip); return; }
        I_Render3DProfile(1, 0);
        /* The screenshot frame runs unpaused so no PAUSE sign covers it. */
        paused = false;
        state = SHOT_PREP;
        return;
    case SHOT_PREP:
        I_Render3DProfile(1, shot_skip);
        paused = true;
        place(&spots[samples[worst].spot], samples[worst].yaw);
        snprintf(shot_path, sizeof(shot_path), "%s/%s.png", out_dir, maps[map_index].name);
        I_Render3DScreenshotTo(shot_path);
        state = SHOT;
        return;
    case SHOT:
        report_map();
    next_map:
        if (++map_index < map_count) { request_map(); return; }
        report_summary();
        state = FINISHED;
        I_Render3DProfile(0, 0);
        I_Quit();
        return;
    case COMBAT: {
        double elapsed=(SDL_GetTicksNS()-combat_start)/1e9;
        combat_cells+=9999-players[consoleplayer].ammo[am_cell];players[consoleplayer].ammo[am_cell]=9999;
        if(fresh) {
            uint64_t now=SDL_GetTicksNS();
            if(elapsed>1&&combat_previous&&combat_count<120000){combat_intervals[combat_count]=(now-combat_previous)/1e6;combat_times[combat_count]=combat_realtime?combat_intervals[combat_count]:cost(frame);++combat_count;}
            combat_previous=now;
        }
        if(elapsed>=combat_seconds+1) {
            for(int i=1;i<combat_count;++i){double v=combat_times[i];int j=i;while(j>0&&combat_times[j-1]>v){combat_times[j]=combat_times[j-1];--j;}combat_times[j]=v;}
            for(int i=1;i<combat_count;++i){double v=combat_intervals[i];int j=i;while(j>0&&combat_intervals[j-1]>v){combat_intervals[j]=combat_intervals[j-1];--j;}combat_intervals[j]=v;}
            int over=0;for(int i=0;i<combat_count;++i)if(combat_times[i]>budget)++over;
            if(!combat_count)I_Error("No live combat frames measured");
            out("Live combat: %d frames, median %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms; %d over %.2f ms.\n",
                combat_count,combat_times[combat_count/2],combat_times[combat_count*95/100],combat_times[combat_count*99/100],combat_times[combat_count-1],over,budget);
            double total=0;int late=0;for(int i=0;i<combat_count;++i){total+=combat_intervals[i];if(combat_intervals[i]>budget)++late;}
            out("Wall-clock (%s): %.1f FPS, p99 interval %.2f ms, max %.2f ms; %d intervals over budget. Plasma cells fired: %d.\n",combat_realtime?"normal rendering":"GPU synchronized",combat_count*1000./total,combat_intervals[combat_count*99/100],combat_intervals[combat_count-1],late,combat_cells);
            if(combat_cells<=0)I_Error("Live combat failed to fire plasma");
            fclose(report);fclose(csv);report=csv=NULL;state=FINISHED;I_Quit();
        }
        return;
    }
    case DOOR: {
        uint64_t now=SDL_GetTicksNS();double elapsed=(now-combat_start)/1e9;
        if(monster_test) {
            if(elapsed>=door_action*2+2){++door_action;monster_snapshot(door_action*2);}
            if(elapsed>=10){fclose(report);fclose(csv);free(door_heights);state=FINISHED;I_Quit();}
            return;
        }
        for(int i=0;i<numsectors;++i) {
            if(door_heights[i*2]!=sectors[i].floorheight||door_heights[i*2+1]!=sectors[i].ceilingheight){door_moving_tic=gametic;++door_changes;}
            door_heights[i*2]=sectors[i].floorheight;door_heights[i*2+1]=sectors[i].ceilingheight;
        }
        if(elapsed>1&&combat_previous)fprintf(csv,"%.6f,%.6f,%d\n",elapsed,(now-combat_previous)/1e6,gametic<=door_moving_tic+1);
        combat_previous=now;
        if(door_action<3&&elapsed>=2+door_action*10){P_UseLines(&players[consoleplayer]);++door_action;}
        if(elapsed>=31) {
            out("Saved door test: %d uses, %d sector-height changes.\n",door_action,door_changes);
            if(door_changes<10)I_Error("Saved door test did not move a door");
            fclose(report);fclose(csv);free(door_heights);state=FINISHED;I_Quit();
        }
        return;
    }
    case FINISHED:
        return;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argv[1][0] == '-')
    {
        fprintf(stderr, "Usage: %s <iwad> [-maps MAP01,E1M2] [-spacing units] [-budget ms] [-out dir] [-strict] [-at x,y[,facing]] [engine options]\n", argv[0]);
        return 1;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    static char *args[256];
    int count = 0, graphics = 0;
    static char graphics_path[1024];
    args[count++] = "perf"; args[count++] = "-iwad"; args[count++] = argv[1];
    for (int i = 2; i < argc && count < 240; ++i)
    {
        if (!strcmp(argv[i], "-maps") && i + 1 < argc) map_filter = argv[++i];
        else if (!strcmp(argv[i], "-door-save") && i + 1 < argc) door_save=argv[++i];
        else if (!strcmp(argv[i], "-monster-save") && i + 1 < argc) {door_save=argv[++i];monster_test=1;}
        else if (!strcmp(argv[i], "-shot-skip") && i + 1 < argc) shot_skip = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-combat-realtime")) combat_realtime=1;
        else if (!strcmp(argv[i], "-combat-seconds") && i + 1 < argc) combat_seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-spacing") && i + 1 < argc) spacing = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-at") && i + 1 < argc)
        {
            char facing[16] = "";
            at_set = sscanf(argv[++i], "%d,%d,%15s", &at_x, &at_y, facing) >= 2;
            for (int k = 0; k < 4; ++k) if (!strcmp(facing, facing_names[k])) at_yaw = k;
        }
        else if (!strcmp(argv[i], "-budget") && i + 1 < argc) budget = atof(argv[++i]);
        else if (!strcmp(argv[i], "-out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "-strict")) strict = 1;
        else
        {
            if (!strcmp(argv[i], "-skill")) skill_given = 1;
            if (!strcmp(argv[i], "-graphics")) graphics = 1;
            args[count++] = argv[i];
        }
    }
    if (spacing < 32 || budget <= 0 || combat_seconds<0 || combat_seconds>120) { fprintf(stderr, "Invalid -spacing or -budget\n"); return 1; }
    if (!graphics)
    {
        /* The player's settings, read but never written. */
        char *preferences = SDL_GetPrefPath("Local Games", "DOOM");
        if (preferences)
        {
            snprintf(graphics_path, sizeof(graphics_path), "%sgraphics.cfg", preferences);
            SDL_free(preferences);
            FILE *file = fopen(graphics_path, "r");
            if (file) { fclose(file); args[count++] = "-graphics"; args[count++] = graphics_path; }
        }
    }
    if (!skill_given) { args[count++] = "-skill"; args[count++] = "4"; }
    args[count++] = "-warp"; args[count++] = "1"; args[count++] = "1";
    args[count++] = "-config"; args[count++] = "perf.cfg";
    args[count++] = "-nomusic";
    int toasty_test=0;for(int i=1;i<argc;++i)if(!strcmp(argv[i],"-test-toasty")||!strcmp(argv[i],"-test-toasty-menu"))toasty_test=1;
    if(!toasty_test)args[count++] = "-nosound";
    myargc = count; myargv = args;
    D_DoomMain();
    return 0;
}

