// Palette-indexed texel reads: R holds the PLAYPAL index, G the WAD coverage.
// Walls and flats also carry palette mip levels (see paletteMips in scene3d/images.cpp);
// uv is in texels of the level read.
vec4 texelAt(sampler2D image,sampler2D palette,ivec2 p,bool wrap,int level) {
    ivec2 size=textureSize(image,level);
    p=wrap?wrapTexel(p,size):clamp(p,ivec2(0),size-1);
    vec2 s=texelFetch(image,p,level).rg;
    vec4 color=texelFetch(palette,ivec2(int(round(s.r*255.0)),0),0);color.a=s.g;
    return color;
}
vec4 texel(sampler2D image,sampler2D palette,ivec2 p,bool wrap) {return texelAt(image,palette,p,wrap,0);}
vec4 indexedAt(sampler2D image,sampler2D palette,vec2 uv,bool filtered,bool wrap,int level) {
    if(!filtered) return texelAt(image,palette,ivec2(floor(uv)),wrap,level);
    vec2 p=uv-0.5;ivec2 q=ivec2(floor(p));vec2 f=fract(p);
    vec4 a=texelAt(image,palette,q,wrap,level),b=texelAt(image,palette,q+ivec2(1,0),wrap,level);
    vec4 c=texelAt(image,palette,q+ivec2(0,1),wrap,level),d=texelAt(image,palette,q+ivec2(1,1),wrap,level);
    a.rgb*=a.a;b.rgb*=b.a;c.rgb*=c.a;d.rgb*=d.a;
    vec4 result=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
    if(result.a>0.0) result.rgb/=result.a;
    return result;
}
vec4 indexed(sampler2D image,sampler2D palette,vec2 uv,bool filtered,bool wrap) {return indexedAt(image,palette,uv,filtered,wrap,0);}
// Sharp bilinear: texel coordinates that hold each texel flat and blend only
// a band of the given width (in texels, at most one) across its edges. Read
// bilinearly, a band one screen pixel wide gives clean edges with no blur.
vec2 sharpen(vec2 uv,vec2 width) {
    vec2 p=uv-0.5,q=floor(p),w=clamp(width,vec2(1e-3),vec2(1.0))*0.5;
    return q+smoothstep(0.5-w,0.5+w,p-q)+0.5;
}
// Glowing eyes (platform/sprite_eyes.h, stamped in scene3d/images.cpp) sit
// in sprite normals no silhouette has: x all the way right, y over halfway
// down. Returns the glow: 1 at the painted spot, 2/3 where it spread.
float eyeGlow(sampler2D image,vec2 uv) {
    vec2 n=texelFetch(image,clamp(ivec2(floor(uv)),ivec2(0),textureSize(image,0)-1),0).ba;
    return n.x>0.995&&n.y>0.74?(n.y*255.0-192.0)/63.0:0.0;
}
// Masks (texture B channel) read as nearest or bilinear with repeat; the
// image is bound with a linear repeat sampler.
float maskAt(sampler2D image,vec2 uv,bool filtered) {
    if(filtered) return textureLod(image,uv/vec2(textureSize(image,0)),0.0).b;
    return texelFetch(image,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0).b;
}
