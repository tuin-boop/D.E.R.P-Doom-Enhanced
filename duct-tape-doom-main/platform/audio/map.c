/* The lines around the listener, gathered from the blockmap, and the
 * crossings of straight paths through them. */
#include <math.h>
#include <stdlib.h>
#include "p_local.h"
#include "map.h"

typedef struct { float x, y, dx, dy; line_t *line; } segment_t;
static segment_t *nearby; static int nearby_count, nearby_capacity;
static int *seen; static int seen_size, seen_stamp;
static float gathered_x, gathered_y; static int gathered_tic = -1;
static line_t *gathered_lines;

void Map_Gather(float x, float y)
{
    if (gathered_lines == lines && gathered_tic == gametic && fabsf(x - gathered_x) < 16 && fabsf(y - gathered_y) < 16)
        return;
    gathered_lines = lines; gathered_tic = gametic; gathered_x = x; gathered_y = y;
    if (seen_size != numlines)
    {
        free(seen); seen = calloc(numlines > 0 ? numlines : 1, sizeof(*seen));
        seen_size = numlines; seen_stamp = 0;
    }
    ++seen_stamp;
    nearby_count = 0;
    float origin_x = bmaporgx / (float)FRACUNIT, origin_y = bmaporgy / (float)FRACUNIT;
    int x0 = (int)floorf((x - MAP_REACH - origin_x) / MAPBLOCKUNITS), x1 = (int)floorf((x + MAP_REACH - origin_x) / MAPBLOCKUNITS);
    int y0 = (int)floorf((y - MAP_REACH - origin_y) / MAPBLOCKUNITS), y1 = (int)floorf((y + MAP_REACH - origin_y) / MAPBLOCKUNITS);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= bmapwidth) x1 = bmapwidth - 1;
    if (y1 >= bmapheight) y1 = bmapheight - 1;
    for (int by = y0; by <= y1; ++by)
        for (int bx = x0; bx <= x1; ++bx)
            for (int32_t *list = blockmaplump + blockmap[by * bmapwidth + bx]; *list != -1; ++list)
            {
                if (*list < 0 || *list >= numlines || seen[*list] == seen_stamp) continue;
                seen[*list] = seen_stamp;
                if (nearby_count == nearby_capacity)
                {
                    nearby_capacity = nearby_capacity ? nearby_capacity * 2 : 1024;
                    nearby = realloc(nearby, sizeof(*nearby) * nearby_capacity);
                }
                line_t *line = &lines[*list];
                nearby[nearby_count++] = (segment_t){line->v1->x / (float)FRACUNIT, line->v1->y / (float)FRACUNIT,
                    line->dx / (float)FRACUNIT, line->dy / (float)FRACUNIT, line};
            }
}

int Map_Cross(float x, float y, float cx, float cy, float length, crossing_t *out, int max)
{
    int count = 0;
    for (int i = 0; i < nearby_count; ++i)
    {
        const segment_t *s = &nearby[i];
        float denominator = cx * s->dy - cy * s->dx;
        if (fabsf(denominator) < 1e-6f) continue;
        float wx = s->x - x, wy = s->y - y;
        float t = (wx * s->dy - wy * s->dx) / denominator, u = (wx * cy - wy * cx) / denominator;
        if (t <= 0.5f || t >= length || u < 0 || u > 1) continue;
        int j;
        if (count < max) j = count++;
        else if (out[max - 1].t > t) j = max - 1;
        else continue;
        for (; j > 0 && out[j - 1].t > t; --j) out[j] = out[j - 1];
        /* Lines face right; a path turning left of one walks into its back. */
        line_t *line = s->line;
        int near_side = denominator < 0 ? 0 : 1;
        sector_t *near = near_side ? line->backsector : line->frontsector;
        sector_t *far = near_side ? line->frontsector : line->backsector;
        if (!(line->flags & ML_TWOSIDED)) far = NULL;
        out[j] = (crossing_t){t, line, near, far, near_side};
    }
    return count;
}
