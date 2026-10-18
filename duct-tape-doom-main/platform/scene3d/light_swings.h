/* The sectors whose light flickers, strobes or glows, and between what
   (light_swings.c). smooth: glowing and fire-flickering sectors, which
   fade rather than switch. */
#ifndef DOOM_LIGHT_SWINGS_H
#define DOOM_LIGHT_SWINGS_H
typedef void (*doom_swing_visit)(int sector, int low, int high, int smooth, void *data);
void doom_light_swings(doom_swing_visit visit, void *data);
#endif
