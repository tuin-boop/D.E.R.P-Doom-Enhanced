#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// A separate depth-tested pass selects emission, so ordinary bright artwork
// and the HUD never become bloom sources. Dark geometry still occludes it.
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D palette;
layout(set=2,binding=2) uniform sampler2D nextImage;
layout(std140,set=3,binding=0) uniform Blend { float blend; };
layout(location=0) out vec4 outColor;
#include "indexed.glsl"
void main() {
    bool sprite=(vMode&MODE_SPRITE)!=0u;
    vec4 color=indexed(image,palette,vUV,false,!sprite);
    if(blend>0.0) color=mix(color,indexed(nextImage,palette,vUV,false,true),blend);
    if(color.a<0.45) discard;
    // Glowing eyes bloom; a spectre's stay hidden.
    float eyes=(vMode&(MODE_GLOWING_EYES|MODE_FUZZ))==MODE_GLOWING_EYES?eyeGlow(image,vUV)*0.6:0.0;
    float e=sprite?((vMode&MODE_PICKUP)!=0u?0.3:eyes):mix(maskAt(image,vUV,true),blend>0.0?maskAt(nextImage,vUV,true):0.0,blend);
    if(!sprite&&(vMode&(MODE_DEAD_GLOW|MODE_LAMP_OFF))!=0u) e=0.0; // A shot-out lamp or screen.
    if(!sprite) e*=unpackUnorm4x8(vLampTint).a; // A fading sector's lamps (lampGlow).
    // White strips glow in every channel at once and wash the view out up
    // close; colored bulbs keep their full bloom.
    float hi=max(color.r,max(color.g,color.b)),lo=min(color.r,min(color.g,color.b));
    float neutral=1.0-saturate((hi-lo)/max(hi,0.001)*2.0);
    outColor=vec4(color.rgb*e*2.5*(1.0-0.6*neutral),1);
}
