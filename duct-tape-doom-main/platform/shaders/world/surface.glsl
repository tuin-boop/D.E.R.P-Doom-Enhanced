// Wall and flat texels: filtered reads with palette mips, relief normals for
// surface detail, and crude materials from the palette ramps.
// Part of world.frag, which declares the resources these read.
// Wall and flat texture reads (c.effects.x: 0 crisp, 1 bilinear, 2 sharp
// bilinear). pixel: texels per screen pixel along u and v. Sharp blends each
// texel edge over one screen pixel plus c.texFilter.x texels, so near views
// keep flat texels with clean edges. Once texels shrink below a pixel,
// palette mip levels (c.texFilter.y) take over; between levels only the
// middle half of each step blends, so the nearer level stays crisp longer.
vec4 surfaceLevel(sampler2D source,vec2 uv,vec2 pixel,int level) {
    vec2 scale=vec2(textureSize(source,level))/vec2(textureSize(source,0));
    uv*=scale;
    if(c.effects.x==2.0) uv=sharpen(uv,pixel*scale+c.texFilter.x);
    return indexedAt(source,palette,uv,c.effects.x>0.0,true,level);
}
vec4 surfaceTexture(sampler2D source,vec2 uv,vec2 pixel,float lod) {
    int top=textureQueryLevels(source)-1;
    bool liquid=(vMode&(MODE_SPRITE|MODE_LIQUID))==MODE_LIQUID;
    if((c.texFilter.y==0.0&&!liquid)||lod<=0.25||top==0) return surfaceLevel(source,uv,pixel,0);
    lod=min(lod,float(top));
    int level=int(floor(lod));float t=smoothstep(0.25,0.75,lod-float(level));
    vec4 near=surfaceLevel(source,uv,pixel,level);
    if(t<=0.0||level>=top) return near;
    vec4 far=surfaceLevel(source,uv,pixel,level+1);
    near.rgb*=near.a;far.rgb*=far.a;
    vec4 result=mix(near,far,t);
    if(result.a>0.0) result.rgb/=result.a;
    return result;
}
// Cotangent frame from screen derivatives: directions of increasing u and
// v, scaled together so the longer is unit length (one texel per map unit).
// Derivatives are taken once in uniform control flow and passed in.
void textureFrame(vec3 normal,vec3 dp1,vec3 dp2,vec2 duv1,vec2 duv2,out vec3 tangent,out vec3 bitangent) {
    vec3 dp2perp=cross(dp2,normal),dp1perp=cross(normal,dp1);
    tangent=dp2perp*duv1.x+dp1perp*duv2.x;bitangent=dp2perp*duv1.y+dp1perp*duv2.y;
    float scale=inversesqrt(max(1e-12,max(dot(tangent,tangent),dot(bitangent,bitangent))));
    tangent*=scale;bitangent*=scale;
}
// Tilts the face normal against a height slope per texel along u and v.
vec3 tiltNormal(vec3 normal,vec2 slope,vec3 dp1,vec3 dp2,vec2 duv1,vec2 duv2) {
    vec3 tangent,bitangent;textureFrame(normal,dp1,dp2,duv1,duv2,tangent,bitangent);
    return normalize(normal-(slope.x*tangent+slope.y*bitangent));
}
// Surface detail: the texel's relief slope tilts the face normal, so lights
// rake across mortar and panel seams. busy: how grainy the artwork is around
// the texel (0 smooth, 1 coarse). tiles: lay the surface's painted tiles
// unevenly (tiles.glsl).
vec3 detailNormal(vec2 uv,vec3 normal,vec3 dp1,vec3 dp2,vec2 duv1,vec2 duv2,bool tiles,out float busy) {
    ivec2 p=ivec2(floor(uv)),size=textureSize(relief,0);
    vec4 cell=texelFetch(relief,wrapTexel(p,size),0);
    busy=smoothstep(0.06,0.36,cell.b);
    vec2 slope=(cell.rg*255.0-128.0)/127.0;
    if(tiles) slope+=tileSlope(p,size,(vMode&MODE_SPRITE)==0u?vStatic:0u);
    return tiltNormal(normal,slope,dp1,dp2,duv1,duv2);
}
// Crude materials from the palette's ramps: greys and blues read as metal,
// greens as wet slime or panels; browns, tans and flesh stay matte.
float paletteGloss(uint index) {
    if(index>=80u&&index<112u) return 0.35;   // Greys.
    if(index>=192u&&index<208u) return 0.30;  // Blues.
    if(index>=240u&&index<248u) return 0.20;  // Dark blues.
    if(index>=112u&&index<128u) return 0.18;  // Greens.
    if(index>=160u&&index<168u) return 0.12;  // Pale yellows.
    return 0.03;
}
// Highlight sharpness per ramp, for varied highlights: slime and blue panels
// polished, grey metal a little worn, everything else broad.
float paletteShininess(uint index) {
    if(index>=112u&&index<128u) return 40.0;  // Greens.
    if(index>=192u&&index<208u) return 36.0;  // Blues.
    if(index>=240u&&index<248u) return 36.0;  // Dark blues.
    if(index>=80u&&index<112u) return 28.0;   // Greys.
    if(index>=160u&&index<168u) return 16.0;  // Pale yellows.
    return 12.0;
}
