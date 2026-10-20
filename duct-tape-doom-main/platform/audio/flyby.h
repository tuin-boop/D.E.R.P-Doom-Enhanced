/* Near misses: an enemy's hitscan shot from far off that passes close by
 * the listener without hitting them whizzes past (IMPACT_FLYBY).
 * Unrealistic, but it tells the player they are under fire when the shooter
 * is too far off to hear well. */
#ifndef DOOM_AUDIO_FLYBY_H
#define DOOM_AUDIO_FLYBY_H

struct mobj_s;

/* A shot from (x1,y1,z1) that stopped at (x2,y2,z2), map units, fired by
   shooter and striking hit (NULL: nothing that bleeds). Returns 1 when it
   passed the listener close enough to hear, with where it was closest and
   how loud, 0..1. */
int Flyby_Near(const struct mobj_s *listener, const struct mobj_s *shooter, const struct mobj_s *hit,
               float x1, float y1, float z1, float x2, float y2, float z2,
               float *x, float *y, float *z, float *level);

#endif
