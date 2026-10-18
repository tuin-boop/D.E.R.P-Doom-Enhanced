#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Depth-only prepass for soft mist under MSAA: WAD coverage cuts out sprites
// and masked walls like the colored passes.
layout(set=2,binding=0) uniform sampler2D image;
void main() {
    ivec2 size=textureSize(image,0),p=ivec2(floor(vUV));
    p=(vMode&MODE_SPRITE)!=0u?clamp(p,ivec2(0),size-1):wrapTexel(p,size);
    if(texelFetch(image,p,0).g<0.45) discard;
}
