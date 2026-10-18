#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Window glass (scene3d/windows.cpp), blended over the finished world. It
// reads through contrast rather than a wash: nearly clear head-on, with
// grime gathered at the frame and the sill, shine streaks painted on it as
// in Duke Nukem 3D, highlights from the lights in front of it as on glossy
// metal, and from outside the sky mirrored in it, darker toward the ground.
// Everything sits on the pane's own one-unit texel grid, like Doom's walls.
// Broken panes keep only jagged teeth along the frame.
// Vertex: vUV the point on the pane in units, vSun its size, vTint the face
// normal (xy) and whether the face is outdoors (z), vLight that side's light,
// vStatic the pane's seed with the top bit set once broken.
layout(set=2,binding=0) uniform sampler2D sky;
layout(set=2,binding=1) uniform sampler2D palette;
// Glass reflections: the world drawn again mirrored in the chosen pane
// (drawGlassReflection in gpu3d/frame.cpp).
layout(set=2,binding=2) uniform sampler2D mirror;
// Glass probes: a row of six cube faces per probe (+x, -x, +y, -y, +z, -z;
// captureGlassProbes in gpu3d/frame.cpp).
layout(set=2,binding=3) uniform sampler2D probes;
// The lights' blockers (buildLightWords in scene3d/lights.cpp), numbered after
// the samplers in SPIR-V and after the four uniform blocks on Metal.
#define BLOCKER_BINDING 4
layout(std430,set=2,binding=BLOCKER_BINDING) readonly buffer Blockers { uvec4 lightWords[]; };
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(std140,set=3,binding=1) uniform Lights FLASH_BLOCK;
layout(std140,set=3,binding=2) uniform Fog FOG_BLOCK;
// The world view's corner (xy) and inverse size (zw) on screen, to read the
// mirrored view (z is 0 when no pane is mirrored this frame); the probe face
// size (probeInfo.x) and how far the mirror has faded in over its probe
// (probeInfo.y); per probe its point, and its room box's low and high
// corners (scene3d.h GlassProbe).
layout(std140,set=3,binding=3) uniform Mirror { vec4 mirrorView,probeInfo; vec4 probeData[192]; };
layout(location=0) out vec4 outColor;
#include "indexed.glsl"
#define BLOCKERS
#include "lighting.glsl"
float hash(vec2 p,uint seed) {
    uvec2 q=uvec2(ivec2(floor(p))+32768);
    uint h=q.x*73856093u^q.y*19349663u^seed;
    // Chris Wellons' lowbias32 (hash-prospector, public domain).
    h^=h>>16u;h*=0x7feb352du;h^=h>>15u;h*=0x846ca68bu;h^=h>>16u;
    return float(h&0xffffffu)/16777216.0;
}
// How deep the broken glass's teeth reach in from one edge at position x
// along it: random depths every 7 units joined by straight cuts.
float tooth(float x,uint seed) {
    float cell=x/7.0,f=fract(cell),i=floor(cell);
    return mix(hash(vec2(i,0),seed),hash(vec2(i+1.0,0),seed),f)*11.0+1.0;
}
// The sky as sky.frag maps it, along a ray; below the horizon it falls off
// to a dark ground so the reflection keeps a hard bright-over-dark line.
vec3 skyAlong(vec3 ray) {
    vec2 size=vec2(textureSize(sky,0));
    float angle=atan(ray.y,ray.x);
    float rise=ray.z/max(length(ray.xy),1e-4);
    vec2 uv=vec2(-angle/6.2831853*size.x*4.0,clamp(0.70-rise*0.68,0.001,0.999)*size.y);
    vec3 color=indexed(sky,palette,uv,false,true).rgb;
    return mix(color,color*0.12,step(rise,-0.02));
}
// What a probe sees along ray from point: the ray runs on to where it leaves
// the probe's room box, and the cube face looking that way from the probe
// holds what is there (box projection), so near walls stay in place as the
// eye moves. Faces as captureGlassProbes draws them: forward, up, and
// right = forward x up, 90 degrees across; read texel by texel.
vec3 probeAlong(uint probe,vec3 point,vec3 ray) {
    vec3 origin=probeData[3u*probe].xyz,low=probeData[3u*probe+1u].xyz,high=probeData[3u*probe+2u].xyz;
    vec3 safe=mix(vec3(1e-4),ray,greaterThan(abs(ray),vec3(1e-4)));
    vec3 leave=max((high-point)/safe,(low-point)/safe);
    vec3 d=point+ray*max(min(min(leave.x,leave.y),leave.z),0.0)-origin;
    vec3 a=abs(d);
    int face=a.x>=a.y&&a.x>=a.z?(d.x>0.0?0:1):a.y>=a.z?(d.y>0.0?2:3):(d.z>0.0?4:5);
    const vec3 forwards[6]=vec3[6](vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1));
    const vec3 ups[6]=vec3[6](vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,1,0),vec3(0,1,0));
    vec3 forward=forwards[face],up=ups[face],right=cross(forward,up);
    float depth=max(dot(d,forward),1e-4),size=probeInfo.x;
    vec2 at=vec2(dot(d,right),-dot(d,up))/depth*0.5+0.5;
    ivec2 texel=ivec2(clamp(floor(at*size),vec2(0),vec2(size-1.0)));
    return texelFetch(probes,texel+ivec2(face,int(probe))*int(size),0).rgb;
}
// Premultiplied layering, front layer last.
void over(inout vec4 result,vec3 color,float alpha) {result=vec4(color*alpha+result.rgb*(1.0-alpha),alpha+result.a*(1.0-alpha));}
void main() {
    vec2 extent=vSun,texel=floor(vUV)+0.5;
    uint seed=vStatic&0x7fffffffu;
    bool broken=(vStatic&0x80000000u)!=0u;
    vec3 normal=vec3(vTint.xy,0.0);
    vec3 toEye=c.eye.xyz-vWorld;
    float distance=length(toEye);
    vec3 view=toEye/max(distance,1e-3);
    float facing=saturate(dot(view,normal));
    float grazing=pow(1.0-facing,3.0);
    // Distance to the frame in texels, and the jagged break.
    float edge=min(min(texel.x,extent.x-texel.x),min(texel.y,extent.y-texel.y));
    float cut=0.0;
    if(broken) {
        float bottom=tooth(texel.x,seed),top=tooth(texel.x+311.0,seed),left=tooth(texel.y+97.0,seed),right=tooth(texel.y+503.0,seed);
        cut=max(max(bottom-texel.y,top-(extent.y-texel.y)),max(left-texel.x,right-(extent.x-texel.x)));
        if(cut<0.0) discard;
    }
    float light=vLight;
    if(c.effects.z==0.0) light*=clamp(1.0-vDistance/3200.0,0.35,1.0);
    vec4 result=vec4(0);
    // The glass itself: faint green-grey, thicker to the eye at a glance.
    over(result,vec3(0.38,0.5,0.5)*light,0.15+0.25*grazing);
    // A pane in the mirror's plane (lightMask.x) shows the world drawn again
    // behind it, read at its own screen position like the liquid mirror,
    // in whole reflection pixels in retro style. It is already lit and
    // exposed. Reflectance rises toward a glance, and bright things (lamps,
    // sky, fire, shots) hold up while dim walls stay faint, so it reads
    // through contrast rather than as a film. Other panes take the probe on
    // the eye's side (vLampTint: probe+1) the same way; without one they
    // fall back to the sky outdoors, and indoors to the dim room, barely.
    bool live=vLightMask.x!=0u&&mirrorView.z>0.0;
    if(live||vLampTint>0u) {
        vec3 probed=vLampTint>0u?probeAlong(vLampTint-1u,vWorld,reflect(-view,normal)):vec3(0),mirrored=probed;
        if(live) {
            vec2 uv=(gl_FragCoord.xy-mirrorView.xy)*mirrorView.zw;
            if((uint(c.flashlightTint.w)&FEATURE_RETRO_REFLECTIONS)!=0u) {
                ivec2 size=textureSize(mirror,0);
                mirrored=texelFetch(mirror,clamp(ivec2(floor(uv*vec2(size))),ivec2(0),size-1),0).rgb;
            } else mirrored=textureLod(mirror,uv,0.0).rgb;
            // A pane that just became the mirror fades in from its probe.
            mirrored=mix(probed,mirrored/max(c.fx.x,0.01),vLampTint>0u?probeInfo.y:1.0);
        }
        float bright=smoothstep(0.08,0.6,dot(mirrored,vec3(0.299,0.587,0.114)));
        over(result,mirrored,(0.22+0.45*grazing)*mix(0.35,1.0,bright)*(live&&vLampTint==0u?probeInfo.y:1.0));
    }
    else if(vTint.z>0.5) over(result,skyAlong(reflect(-view,normal))*max(light,0.5),0.18+0.5*grazing);
    else over(result,vec3(0.3,0.33,0.32)*light,0.05+0.25*grazing);
    // Grime: dust settled along the frame and thickest on the sill, single
    // texels gathered in patches.
    float dirt=0.7*(1.0-smoothstep(0.0,12.0,edge))+0.45*(1.0-smoothstep(0.0,20.0,texel.y));
    dirt*=0.3+1.2*hash(floor(texel/4.0),seed^0x2545f491u);
    if(hash(texel,seed^0x9e3779b9u)<dirt) over(result,vec3(0.24,0.22,0.17)*light,0.3+0.3*hash(texel,seed^0x51ed27u));
    // Highlights as on glossy metal (surface_light.glsl): the lights on the
    // eye's side of the glass, walls between included, mirrored in it as a
    // tight Blinn-Phong spot that slides as the eye or the light moves, a
    // little stronger at a glance. Static lamps near the eye are among them
    // as dynamic copies. Outdoors the sun shows the same way, tighter.
    // The same lights also light the glass itself (lit), for the streaks.
    vec3 mirrored=reflect(-view,normal),specular=vec3(0),lit=vec3(light);
    float gloss=1.2*(0.7+0.6*grazing);
    float stipple=flashStipple(vWorld,normal);
    for(uint i=0u;i<flashCount;++i) {
        float amount=flashAt(vWorld,normal,i,stipple);
        if(amount<=0.0) continue;
        vec3 toLight=normalize(lights[i].position.xyz-vWorld);
        float front=saturate(dot(normal,toLight)*4.0);
        float highlight=pow(saturate(dot(normal,normalize(toLight+view))),128.0);
        specular+=lights[i].color.rgb*amount*highlight*gloss*front;
        lit+=lights[i].color.rgb*amount*front;
    }
    // Shine streaks painted on the glass, as in Duke Nukem 3D's window
    // texture: fixed to the pane, never sliding. They slant up across it in
    // groups, one per 56-unit cell along the pane where a cell has one: a
    // wide stripe with two thin ones, two thin ones, or a single thin one,
    // each group at its own height, length and strength, tapering to points
    // at both ends. They are lit like the glass, so a flashlight beam makes
    // them flare.
    float across=texel.x-texel.y*0.75+extent.y;
    float cell=floor(across/56.0);
    float inCell=across-cell*56.0-4.0-18.0*hash(vec2(cell,1),seed);
    float kind=hash(vec2(cell,2),seed);
    float middle=extent.y*(0.3+0.4*hash(vec2(cell,3),seed));
    float reach=max(extent.y*(0.2+0.18*hash(vec2(cell,4),seed)),12.0);
    float taper=1.0-pow((texel.y-middle)/reach,2.0);
    float streak=0.0;
    if(kind<0.85&&taper>0.0) {
        float wide=kind<0.5?6.0:0.0;
        if(inCell>=0.0&&inCell<wide*taper) streak=1.0;
        else if(inCell>=wide+3.0&&inCell<wide+3.0+3.0*taper) streak=0.8;
        else if(kind<0.68&&inCell>=wide+8.0&&inCell<wide+8.0+2.0*taper) streak=0.6;
        streak*=0.7+0.3*hash(vec2(cell,5),seed);
    }
    if(streak>0.0) {
        // A whole pane's streaks keep some light so they read in dim rooms;
        // the teeth of a broken one take only the light there.
        vec3 shade=vec3(0.88,0.96,1.0)*min(broken?lit:0.45+lit*0.75,vec3(1.2));
        over(result,saturate(shade),streak*(0.3+0.22*grazing));
    }
    if(vTint.z>0.5&&c.sun.w>0.0) {
        vec3 sun=vec3(cos(c.fx2.z)*0.76604,sin(c.fx2.z)*0.76604,0.64279);
        specular+=c.sun.rgb*c.sun.w*pow(saturate(dot(mirrored,sun)),400.0)*gloss*1.5;
    }
    specular=saturate(specular);
    float shine=max(specular.r,max(specular.g,specular.b));
    if(shine>0.004) over(result,specular/shine,shine);
    // A fresh break catches the light along its edge: the room's and the
    // dynamic lights', so it glints under the flashlight and stays dark in
    // a dark room.
    if(broken&&cut<1.5) over(result,vec3(0.85,0.95,1.0)*min(lit,vec3(1.2)),0.45);
    if(result.a<=0.002) discard;
    vec3 color=result.rgb/result.a*c.fx.x;
    if(c.materials.z>0.0&&c.effects.z==0.0)
        result.a*=exp(-heightFogTau(c.eye.z,vWorld.z,distance,c.fog.x,c.fog.y,c.fog.z));
    outColor=vec4(saturate(color),result.a);
}
