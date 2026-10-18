// Monitor glass in computer textures: where a texel sits on its screen and
// the curved CRT dome over it.
// Part of world.frag, which declares the resources these read.
bool isGlass(vec2 uv) {
    return texelFetch(image,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0).a<0.999;
}
// Position across the screen in [-1,1], and the screen's top-left texel and
// size in texels.
vec2 glassScreen(vec2 uv,out vec2 origin,out vec2 extent) {
    vec4 f=texelFetch(glassFrame,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0)*255.0;
    origin=floor(uv)-round(f.rg);extent=max(round(f.ba),vec2(1));
    return clamp((uv-origin)/extent*2.0-1.0,vec2(-1),vec2(1));
}
// A shot-out screen (MODE_DEAD_GLOW): cracks run out from where the shot
// struck, set per screen and texture repeat, and on the texel grid, plus a
// ring of fracture through some of the wedges. Returns 1 on a crack texel;
// shard numbers the wedge between two cracks the texel lies in.
float glassCracks(vec2 uv,vec2 origin,vec2 extent,uint seed,out uint shard) {
    const int rays=6;
    vec2 texel=floor(uv)+0.5;
    ivec2 repeat=ivec2(floor(uv/vec2(textureSize(image,0))));
    uint h=tileHash(repeat*1024+ivec2(origin),seed);
    vec2 hit=origin+extent*(0.3+0.4*vec2(float(h&255u),float((h>>8)&255u))/255.0);
    vec2 d=texel-hit;
    float r=length(d),angle=atan(d.y,d.x)+3.14159265,crack=r<1.0?1.0:0.0;
    shard=0u;
    for(int i=0;i<rays;++i) {
        float a=(float(i)+0.8*float((h>>(16+2*i))&3u)/3.0)*6.2831853/float(rays);
        if(angle>a) shard=uint(i+1);
        vec2 along=vec2(cos(a-3.14159265),sin(a-3.14159265));
        if(dot(d,along)>0.0&&abs(d.x*along.y-d.y*along.x)<0.5) crack=1.0;
    }
    if(shard==uint(rays)) shard=0u; // The last wedge wraps into the first.
    float ring=min(extent.x,extent.y)*(0.3+0.2*float((h>>28)&3u)/3.0);
    if(abs(r-ring)<0.5&&(tileHash(ivec2(int(shard),0),h)&1u)!=0u) crack=1.0;
    return crack;
}
// The CRT dome, peaking at a quarter of the shorter side: its smooth normal.
vec3 glassDome(vec2 screen,vec2 extent,vec3 normal,vec3 tangent,vec3 bitangent) {
    float peak=min(extent.x,extent.y)*0.25;
    vec2 slope=-peak*4.0*screen*(1.0-screen.yx*screen.yx)/extent;
    return normalize(normal-(slope.x*tangent+slope.y*bitangent));
}
