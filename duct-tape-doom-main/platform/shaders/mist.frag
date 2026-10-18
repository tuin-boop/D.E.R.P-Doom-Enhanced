#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D cloud;
layout(set=2,binding=1) uniform sampler2D depth;
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(location=0) out vec4 outColor;
float heightFogTau(float oz,float pz,float distance,float density,float falloff,float reference) {
    float x=falloff*abs(pz-oz);
    float f=x<1e-3?1.0-x*0.5+x*x/6.0:(1.0-exp(-x))/x;
    return density*distance*exp(clamp(-falloff*(min(oz,pz)-reference),-80.0,80.0))*f;
}
void main() {
    float d=texelFetch(depth,ivec2(gl_FragCoord.xy),0).r;
    float sceneZ=c.projection.z*c.projection.w/(c.projection.w-d*(c.projection.w-c.projection.z));
    float layerZ=dot(vWorld-c.eye.xyz,c.forward.xyz);
    vec3 delta=vWorld-c.eye.xyz;
    float distance=length(delta);
    float transmission=exp(-heightFogTau(c.eye.z,vWorld.z,distance,c.fog.x,c.fog.y,c.fog.z));
    if((vMode&MODE_SUNBEAM)!=0u) {
        // Sunbeam ribbon (buildShafts), added to the scene: u runs across
        // (-1..1), v from the floor (0) to past the opening (1). Soft at the
        // sides, the ends and where it meets geometry, faded near the eye, with
        // cloud noise drifting up the beam a step per tic. The result keeps
        // to 1/32 steps on an ordered dither in Doom pixels.
        float across=1.0-vUV.x*vUV.x;
        float along=smoothstep(0.0,0.1,vUV.y)*(1.0-smoothstep(0.55,1.0,vUV.y));
        float near=smoothstep(16.0,80.0,distance)*saturate((sceneZ-layerZ)/24.0);
        float tic=floor(c.fog.w*35.0);
        float noise=textureLod(cloud,vec2(dot(vWorld.xy,vec2(0.7071)),vWorld.z-tic*0.35)/112.0,0.0).r;
        float glow=vLight*across*across*along*near*(0.5+noise)*0.05;
        glow=floor(glow*32.0+bayer4(ivec2(floor(gl_FragCoord.xy/max(c.fx2.x,1.0)))))/32.0;
        outColor=vec4(vTint*c.fx.x*transmission,glow);
        return;
    }
    float soft=saturate((sceneZ-layerZ)/8.0);
    // Suppress sheets edge-on and soften the camera crossing a layer.
    soft*=smoothstep(0.025,0.18,abs(delta.z)/max(distance,0.001));
    soft*=smoothstep(0.0,3.0,abs(delta.z));
    vec2 uv=vWorld.xy/160.0+vec2(c.fog.w*0.009,c.fog.w*0.005)+vUV;
    float noise=textureLod(cloud,uv,0.0).r;
    float alpha=vLight*(0.25+noise*0.75)*soft;
    outColor=vec4(mix(fogHaze(c.fx.yzw),vTint,transmission)*c.fx.x,alpha);
}
