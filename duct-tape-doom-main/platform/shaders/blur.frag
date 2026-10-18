#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(std140,set=3,binding=0) uniform Direction { vec2 direction; };
layout(location=0) out vec4 outColor;
void main() {
    vec3 glow=textureLod(image,vUV,0.0).rgb*0.227027;
    glow+=(textureLod(image,vUV+direction*1.384615,0.0).rgb+
           textureLod(image,vUV-direction*1.384615,0.0).rgb)*0.316216;
    glow+=(textureLod(image,vUV+direction*3.230769,0.0).rgb+
           textureLod(image,vUV-direction*3.230769,0.0).rgb)*0.070270;
    outColor=vec4(glow,1);
}
