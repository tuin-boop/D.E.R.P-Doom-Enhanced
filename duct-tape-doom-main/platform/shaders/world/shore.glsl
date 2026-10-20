// Damp shores above liquids.
// Part of world.frag, which declares the resources these read.
// Shorelines (FEATURE_DAMP_SHORES). Liquids wet what they touch: walls and banks
// above the waterline turn darker up to a ragged line, read at texel
// centers so the damp edge steps in whole Doom pixels.
const float shoreRange=16.0;
float shoreDistance(vec2 xy) {
    return textureLod(shore,(xy-c.map.xy)*c.map.z/vec2(textureSize(shore,0)),0.0).r*2.0*shoreRange-shoreRange;
}
uvec4 shoreCell(vec2 xy) {
    ivec2 size=textureSize(shore,0);
    return uvec4(round(texelFetch(shore,clamp(ivec2(floor((xy-c.map.xy)*c.map.z)),ivec2(0),size-1),0)*255.0));
}
// The nearest edge's liquid sector and tint; false where none is in range.
bool shoreLiquid(vec2 xy,out uint liquid,out vec3 tint) {
    uvec4 v=shoreCell(xy);
    liquid=v.g|((v.b&63u)<<8);
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
// Soft value noise on whole cells of p.
float poolNoise(vec2 p) {
    vec2 i=floor(p),f=smoothstep(0.0,1.0,fract(p));
    return mix(mix(shoreHash(i),shoreHash(i+vec2(1,0)),f.x),mix(shoreHash(i+vec2(0,1)),shoreHash(i+vec2(1,1)),f.x),f.y);
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
    // Hot liquids (tint 0) leave no damp.
    if(!shoreLiquid(at,liquid,tint)||tint==vec3(0)) {tint=vec3(1);return 0.0;}
    float d=shoreDistance(at),water=sectorInfo[liquid].y;
    if(!wall&&d>0.0&&abs(sectorInfo[sector].y-water)<0.5) return 0.0;
    float height=(wall?world.z:sectorInfo[sector].y)-water;
    if(height<-0.5) return 0.0;
    float along=floor(dot(world.xy,vec2(-normal.y,normal.x)));
    vec2 wander=wall?vec2(along,0):floor(world.xy),texel=wall?vec2(along,floor(world.z)):floor(world.xy);
    return dampness(max(height,0.0),abs(d),wander,texel,luma);
}
// Glowing liquid edges (FEATURE_GLOW_EDGES). Vanilla walls take one texture,
// so only walls a mapper gave an edge texture (NUKE24) show the strip of
// glowing liquid at their foot; walls running straight into the pool stay
// bare. Walls at a glowing liquid's edge take that strip (liquid_edges.cpp,
// stored below the caustic frames): the edge texture's own texels, row by row
// up from the liquid's surface, painted over the wall where the art is
// liquid. Returns the paint (0 none) with the strip's color and glow.
// Only walls standing at the shore itself, not behind a bank.
float edgeStrip(vec3 world,vec3 normal,out vec3 paint,out float glow) {
    paint=vec3(0);glow=0.0;
    vec2 at=world.xy+normal.xy*0.25;
    uvec4 v=shoreCell(at);
    if((v.b&192u)!=192u||abs(shoreDistance(at))>2.0) return 0.0;
    float water=sectorInfo[v.g|((v.b&63u)<<8)].y;
    int base=max(1,textureSize(causticPattern,0).y/(64*7))*64*7;
    int rows=int(round(texelFetch(causticPattern,ivec2(0,base),0).r*255.0));
    float height=world.z-water;
    int row=int(floor(height));
    if(rows==0||row<0||row>=rows) return 0.0;
    int x=int(floor(dot(world.xy,vec2(-normal.y,normal.x))))&63;
    float index=texelFetch(causticPattern,ivec2(x,base+1+row),0).r;
    float amount=texelFetch(causticPattern,ivec2(x,base+1+rows+row),0).r;
    glow=texelFetch(causticPattern,ivec2(x,base+1+2*rows+row),0).r;
    paint=texelFetch(palette,ivec2(int(round(index*255.0)),0),0).rgb;
    return amount;
}
