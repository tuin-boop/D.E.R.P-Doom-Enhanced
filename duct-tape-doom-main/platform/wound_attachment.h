/* Sprite-frame attachment for wound holes. Doom's sprite artwork has no
   shared UVs or skeleton: an opaque-row position supplies a pose-relative
   estimate, then a small palette/coverage patch follows nearby artwork.
   This is a bounded correspondence heuristic for frames of the same view,
   not a reconstruction of anatomy. Always map from the original hit frame;
   chaining frame-to-frame matches would accumulate drift. */
#ifndef DOOM_WOUND_ATTACHMENT_H
#define DOOM_WOUND_ATTACHMENT_H

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

namespace doom_wounds {
struct Frame { int width,height,left,top; const unsigned char *pixels; };
struct Point { float u,v; };

namespace detail {
struct Bounds { int left,top,right,bottom; bool any=false; };
inline bool opaque(const Frame &f,int x,int y) {
    return f.pixels&&x>=0&&y>=0&&x<f.width&&y<f.height&&
        f.pixels[((std::size_t)y*f.width+x)*2+1]!=0;
}
inline Bounds bounds(const Frame &f) {
    Bounds b={f.width,f.height,0,0,false};
    if(!f.pixels||f.width<=0||f.height<=0)return b;
    for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x)if(opaque(f,x,y)) {
        b.left=std::min(b.left,x);b.right=std::max(b.right,x+1);
        b.top=std::min(b.top,y);b.bottom=std::max(b.bottom,y+1);b.any=true;
    }
    return b;
}
inline bool row(const Frame &f,int y,int &left,int &right) {
    left=f.width;right=0;
    for(int x=0;x<f.width;++x)if(opaque(f,x,y)) {left=std::min(left,x);right=x+1;}
    return left<right;
}
// Exact palette matches carry most weight; nearby indices help with the
// ramps in Doom's palette without treating distant ramps as nearby colors.
inline float paletteDifference(unsigned char a,unsigned char b) {
    if(a==b)return 0;
    return 0.45f+std::min(32,std::abs((int)a-(int)b))/64.0f;
}
inline float patchCost(const Frame &s,const Frame &d,int sx,int sy,int dx,int dy) {
    float cost=0,weight=0;
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
        bool a=opaque(s,sx+x,sy+y),b=opaque(d,dx+x,dy+y);
        if(!a&&!b)continue;
        float w=x==0&&y==0?3.0f:1.0f;
        weight+=w;
        if(a!=b)cost+=w*1.5f;
        else cost+=w*paletteDifference(s.pixels[((std::size_t)(sy+y)*s.width+sx+x)*2],
                                      d.pixels[((std::size_t)(dy+y)*d.width+dx+x)*2]);
    }
    return weight?cost/weight:1.5f;
}
} // namespace detail

inline Point map(const Frame &source,const Frame &dest,Point point) {
    // Texture coordinates are edges (a texel's center is n+0.5). Callers
    // handle mirroring so comparison always uses the authored artwork.
    if(source.pixels==dest.pixels&&source.width==dest.width&&source.height==dest.height)
        return point;
    Point estimate={point.u+dest.left-source.left,point.v+dest.top-source.top};
    auto sb=detail::bounds(source),db=detail::bounds(dest);
    if(!sb.any||!db.any)return estimate;
    if(!std::isfinite(point.u)||!std::isfinite(point.v))return estimate;
    int sx=(int)std::floor(point.u),sy=(int)std::floor(point.v);
    if(!detail::opaque(source,sx,sy))return estimate;

    // Silhouette height tracks bobbing/crouching; the occupied span of a
    // row tracks sideways sway and supplies a fallback in untextured areas.
    float height=(float)(db.bottom-db.top)/(sb.bottom-sb.top);
    estimate.v=db.top+(point.v-sb.top)*height;
    int sl,sr,dl,dr;
    if(detail::row(source,sy,sl,sr)&&
       detail::row(dest,std::clamp((int)std::floor(estimate.v),db.top,db.bottom-1),dl,dr))
        estimate.u=dl+(point.u-sl)*(dr-dl)/(sr-sl);
    else estimate.u=db.left+(point.u-sb.left)*(db.right-db.left)/(sb.right-sb.left);

    int radiusX=std::clamp((int)std::ceil(std::max(source.width,dest.width)*0.24f),6,24);
    int radiusY=std::clamp((int)std::ceil(std::max(source.height,dest.height)*0.18f),5,20);
    int cx=(int)std::floor(estimate.u),cy=(int)std::floor(estimate.v);
    float best=std::numeric_limits<float>::max();
    int bestX=-1,bestY=-1;
    for(int y=std::max(db.top,cy-radiusY);y<=std::min(db.bottom-1,cy+radiusY);++y)
        for(int x=std::max(db.left,cx-radiusX);x<=std::min(db.right-1,cx+radiusX);++x) {
            if(!detail::opaque(dest,x,y))continue;
            float dx=(x-cx)/(float)radiusX,dy=(y-cy)/(float)radiusY;
            // A weak displacement prior breaks repeated-texture ties while
            // allowing a distinctive limb/clothing patch to move in its pose.
            float cost=detail::patchCost(source,dest,sx,sy,x,y)+0.12f*(dx*dx+dy*dy);
            if(cost<best) {best=cost;bestX=x;bestY=y;}
        }
    if(bestX<0)return estimate;
    return {bestX+(point.u-sx),bestY+(point.v-sy)};
}
} // namespace doom_wounds
#endif
