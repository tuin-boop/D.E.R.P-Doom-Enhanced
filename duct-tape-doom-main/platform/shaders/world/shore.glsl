// Damp shores above liquids.
// Part of world.frag, which declares the resources these read.
// Shorelines (FEATURE_DAMP_SHORES). Liquids wet what they touch: walls and banks
// above the waterline turn darker up to a ragged line, read at texel
// centers so the damp edge steps in whole Doom pixels.
const float shoreRange=16.0;
float shoreDistance(vec2 xy) {
    return textureLod(shore,(xy-c.map.xy)*c.map.z/vec2(textureSize(shore,0)),0.0).r*2.0*shoreRange-shoreRange;
}
// The nearest edge's liquid sector and tint; false where none is in range.
bool shoreLiquid(vec2 xy,out uint liquid,out vec3 tint) {
    ivec2 size=textureSize(shore,0);
    uvec4 v=uvec4(round(texelFetch(shore,clamp(ivec2(floor((xy-c.map.xy)*c.map.z)),ivec2(0),size-1),0)*255.0));
    liquid=v.g|((v.b&127u)<<8);
    tint=vec3(float(v.a>>5)/7.0,float((v.a>>2)&7u)/7.0,float(v.a&3u)/3.0);
    return (v.b&128u)!=0u;
}
float shoreHash(vec2 p) {
    uvec2 q=uvec2(ivec2(floor(p)));
    uint h=q.x*1664525u^(q.y*1013904223u+0x9e3779b9u);
    // Chris Wellons' lowbias32 (hash-prospector, public domain).
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return float(h)/4294967295.0;
}
// How wet a point is, in quarter steps: the liquid wicks up to a ragged line
// 6-19 units above it, less the farther across a bank, wandering every few
// texels (wander) and climbing where the artwork is darker, as mortar and
// cracks soak up more. texel adds a per-texel jitter.
float dampness(float height,float across,vec2 wander,vec2 texel,float luma) {
    vec2 k=wander/6.0,i=floor(k),f=smoothstep(0.0,1.0,fract(k));
    float drift=mix(mix(shoreHash(i),shoreHash(i+vec2(1,0)),f.x),mix(shoreHash(i+vec2(0,1)),shoreHash(i+vec2(1,1)),f.x),f.y);
    float reach=6.0+7.0*drift+5.0*(1.0-saturate(luma*2.0))+1.5*shoreHash(texel);
    return round(saturate((reach-height-across*1.5)/3.0)*4.0)/4.0;
}
// Walls read the edge just off their visible side, floors at their texel's
// center. A floor at its liquid's height on the liquid's side of the edge is
// the liquid itself and stays as it is.
float shoreWet(vec3 world,vec3 normal,uint sector,float luma,out vec3 tint) {
    tint=vec3(1);
    bool wall=normal.z<=0.5;
    vec2 at=wall?world.xy+normal.xy*0.25:floor(world.xy)+0.5;
    uint liquid;
    if(!shoreLiquid(at,liquid,tint)) return 0.0;
    float d=shoreDistance(at),water=sectorInfo[liquid].y;
    if(!wall&&d>0.0&&abs(sectorInfo[sector].y-water)<0.5) return 0.0;
    float height=(wall?world.z:sectorInfo[sector].y)-water;
    if(height<-0.5) return 0.0;
    float along=floor(dot(world.xy,vec2(-normal.y,normal.x)));
    vec2 wander=wall?vec2(along,0):floor(world.xy),texel=wall?vec2(along,floor(world.z)):floor(world.xy);
    return dampness(max(height,0.0),abs(d),wander,texel,luma);
}
