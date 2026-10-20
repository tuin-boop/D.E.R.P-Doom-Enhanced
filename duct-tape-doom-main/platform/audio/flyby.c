/* Whether a shot went past the listener; see flyby.h. The bullet's path is a
 * straight segment; the point on it nearest the listener's head is where it
 * whizzes. It counts only if the bullet got past that point (it didn't stop
 * in a wall in front of them, which its impact already says) and came within
 * a couple of metres. Only far shots whizz: in a close fight the gunshots
 * themselves say plenty, but a shooter across a courtyard is a faint pop. */
#include <math.h>
#include "doomdef.h"
#include "p_local.h"
#include "flyby.h"

#define FLYBY_REACH 80.0f  /* map units from the head (~2.5 m) */
#define HEAD 41.0f         /* the listener's ear above their feet */
#define FAR_FROM 640.0f    /* shooters nearer than this (~20 m) never whizz... */
#define FAR_FULL 960.0f    /* ...and from this far (~30 m) on, fully */

int Flyby_Near(const struct mobj_s *listener, const struct mobj_s *shooter, const struct mobj_s *hit,
               float x1, float y1, float z1, float x2, float y2, float z2,
               float *x, float *y, float *z, float *level)
{
    if (!listener || shooter == listener || hit == listener) return 0;
    float hx = listener->x / (float)FRACUNIT, hy = listener->y / (float)FRACUNIT;
    float away = sqrtf((hx - x1) * (hx - x1) + (hy - y1) * (hy - y1));
    if (away <= FAR_FROM) return 0;
    float hz = listener->z / (float)FRACUNIT + HEAD;
    float dx = x2 - x1, dy = y2 - y1, dz = z2 - z1, length2 = dx * dx + dy * dy + dz * dz;
    if (length2 < 1) return 0;
    float t = ((hx - x1) * dx + (hy - y1) * dy + (hz - z1) * dz) / length2;
    /* Past the shooter, and the bullet flew on beyond the listener. */
    if (t <= 0 || t >= 1) return 0;
    float px = x1 + t * dx, py = y1 + t * dy, pz = z1 + t * dz;
    float miss = sqrtf((hx - px) * (hx - px) + (hy - py) * (hy - py) + (hz - pz) * (hz - pz));
    if (miss >= FLYBY_REACH) return 0;
    *x = px; *y = py; *z = pz;
    *level = (1 - 0.6f * miss / FLYBY_REACH) * fminf((away - FAR_FROM) / (FAR_FULL - FAR_FROM), 1);
    return 1;
}
