#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Debris: flat colored billboards; tint is the lit color, light the opacity.
// SOLID: the crosshair. HEAT: hot-air volumes lower the target's alpha to
// 1-heat (alpha-only writes, MIN blending); present.frag shimmers there.
layout(location=0) out vec4 outColor;
void main() {
#ifdef SOLID
    outColor=vec4(0.85,1,0.85,0.8);
#elif defined(HEAT)
    outColor=vec4(0,0,0,1.0-saturate(vLight));
#else
    outColor=vec4(vTint,vLight);
#endif
}
