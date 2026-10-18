/* Legacy distance-haze reference and the retained analytic light-glow limits.
 * Height extinction itself lives in platform/shaders/lighting.glsl. */
#ifndef DOOM_VOLUMETRIC_FOG_H
#define DOOM_VOLUMETRIC_FOG_H
#include <math.h>
#define DOOM_FOG_DENSITY 0.00035f
#define DOOM_FOG_RANGE 2000.0f
static inline float doom_fog_transmittance(float distance,int enabled) {
    return enabled?expf(-DOOM_FOG_DENSITY*fminf(DOOM_FOG_RANGE,fmaxf(0,distance))):1;
}
/* Keep equivalent to fogBeamIntegral in platform/shaders/lighting.glsl. */
static inline float doom_fog_beam_integral(float distance,float radius) {
    if(radius<=0)return 0;
    float u=fminf(1,fmaxf(0,distance/radius));
    return radius*(u-u*u*u+0.5f*u*u*u*u);
}
#endif
