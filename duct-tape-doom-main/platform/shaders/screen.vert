#version 450
// Full-screen triangle; SKY places it on the far plane.
#extension GL_GOOGLE_include_directive : require
#define VERTEX
#include "common.glsl"
void main() {
    uint i=uint(gl_VertexIndex);vec2 p=vec2((i<<1)&2u,i&2u);
#ifdef SKY
    float z=1.0;
#else
    float z=0.0;
#endif
    gl_Position=vec4(p*vec2(2,-2)+vec2(-1,1),z,1);
    vUV=p;vLight=1;vDistance=0;vMode=MODE_SCREEN;vWorld=vec3(0);vTint=vec3(0);vLightMask=uvec2(0);
}
