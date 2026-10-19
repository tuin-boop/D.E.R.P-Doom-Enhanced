/* Glowing liquid edges: the strip of liquid the game paints at the foot of
   its edge textures (DOOM's NUKE24 and NUKEDGE1), found in the WAD at runtime. */
#include "internal.h"
#include <cctype>
extern "C" { extern int numtextures; }

namespace doom3d {
// An edge texture glows in a liquid's kind along its foot and not at its top:
// rock or metal with the liquid lapping at it. Of those, the one with the
// tallest strip is the liquid's edge. Each row of the strip, counted up from
// the foot, holds its palette indices, how much each texel is the liquid's
// paint (the glowing texels, and the duller ones in its hue that fade into
// the rock) and its glow.
// How a texture glows in a liquid's kind, row by row.
struct GlowProfile {
    int w=0,h=0,rows=0; // rows: glowing rows up from the foot.
    float foot=0; bool topDark=false; float hue[3]={};
    std::vector<byte> pixels,glow; std::vector<float> coverage;
};
static GlowProfile glowProfile(int t,int kind) {
    GlowProfile p;
    R_TextureDimensions(t,&p.w,&p.h);
    if(p.w<8||p.h<8||p.w>1024||p.h>1024)return p;
    int w=p.w,h=p.h;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    p.pixels.resize((size_t)w*h*2);R_CopyIndexedTexture(t,p.pixels.data());
    p.coverage.assign(h,0);p.glow.resize((size_t)w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;const byte *c=palette+p.pixels[2*i]*3;
        p.glow[i]=doom_emissive_pixel(kind,c,p.pixels[2*i+1]);
        if(!p.glow[i])continue;
        p.coverage[y]+=1.0f/w;
        for(int k=0;k<3;++k)p.hue[k]+=c[k];
    }
    p.foot=(p.coverage[h-1]+p.coverage[h-2]+p.coverage[h-3]+p.coverage[h-4])/4;
    p.topDark=true;
    for(int y=0;y<h/4;++y)p.topDark&=p.coverage[y]<0.02f;
    while(p.rows<h&&p.coverage[h-1-p.rows]>=0.02f)++p.rows;
    return p;
}
// Glowing along the foot only, in a strip that ends in the lower half under
// dark rock: an edge texture (solid foot) or a thin one (E1M2's NUKESLAD);
// not a wall of drips or falls.
static bool footOnly(const GlowProfile &p) {return p.h>0&&p.topDark&&p.rows*2<=p.h+8;}
static EdgeStrip findStrip(int kind) {
    EdgeStrip best;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    for(int t=0;t<numtextures;++t) {
        char name[9];R_TextureName(t,name);
        if(doom_emissive_kind(name,0)!=kind)continue;
        GlowProfile p=glowProfile(t,kind);
        if(!footOnly(p)||p.foot<0.9f)continue;
        int w=p.w,h=p.h,rows=p.rows;
        const auto &pixels=p.pixels;const auto &glow=p.glow;const float *hue=p.hue;
        // The paint fades a few rows past the last glowing one.
        rows=std::min({rows+rows/2+1,h-h/4,63});
        if(rows<=best.height)continue;
        int dominant=hue[1]>=hue[0]&&hue[1]>=hue[2]?1:hue[0]>=hue[2]?0:2;
        best.kind=kind;best.height=rows;best.texture=t;
        best.index.assign((size_t)64*rows,0);best.paint.assign((size_t)64*rows,0);best.glow.assign((size_t)64*rows,0);
        for(int r=0;r<rows;++r)for(int x=0;x<64;++x) {
            size_t i=(size_t)(h-1-r)*w+x%w,o=(size_t)r*64+x;
            const byte *c=palette+pixels[2*i]*3;
            int other=std::max(c[(dominant+1)%3],c[(dominant+2)%3]);
            best.index[o]=pixels[2*i];best.glow[o]=glow[i];
            best.paint[o]=glow[i]?255:(byte)std::clamp((c[dominant]-other-8)*255/40,0,255);
        }
    }
    return best;
}
// Whether a wall texture already paints the liquid's edge itself (NUKE24,
// NUKEDGE1) or glows up its face (SLADRIP vents, SFALL falls), so the strip
// stays off it. Walls with only a thin painted foot, or none, take the strip.
bool paintsOwnEdge(int texture,int kind) {
    static std::map<std::pair<int,int>,bool> known;
    auto key=std::make_pair(texture,kind);
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9];R_TextureName(texture,name);
    bool own=false;
    if(doom_emissive_kind(name,0)==kind) {
        GlowProfile p=glowProfile(texture,kind);
        own=p.h>0&&p.coverage[p.h-1]>=0.02f&&(!footOnly(p)||p.foot>=0.9f);
    }
    return known[key]=own;
}
const EdgeStrip &edgeStrip(int kind) {
    static std::map<int,EdgeStrip> strips;
    auto found=strips.find(kind);
    if(found==strips.end())found=strips.emplace(kind,findStrip(kind)).first;
    return found->second;
}
// The glow kind of a sector's liquid floor (DOOM_EMISSIVE_NONE when it is not
// a glowing liquid).
int liquidGlowKind(int sector) {
    if(!sectorMist[sector].liquid||sectors[sector].floorpic==skyflatnum)return DOOM_EMISSIVE_NONE;
    int lump=firstflat+flattranslation[sectors[sector].floorpic];
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    for(char &ch:name)ch=(char)toupper((unsigned char)ch);
    int kind=doom_emissive_kind(name,1);
    return kind==DOOM_EMISSIVE_SLIME||kind==DOOM_EMISSIVE_LAVA||kind==DOOM_EMISSIVE_MAGMA?kind:DOOM_EMISSIVE_NONE;
}
// The level's strip: of the glowing liquids it has an edge texture for, the
// most common one.
const EdgeStrip *levelEdgeStrip() {
    std::map<int,int> use;
    for(int i=0;i<numsectors;++i)if(int kind=liquidGlowKind(i))++use[kind];
    const EdgeStrip *best=nullptr;int most=0;
    for(auto [kind,count]:use) {
        const EdgeStrip &strip=edgeStrip(kind);
        if(strip.height>0&&count>most) {best=&strip;most=count;}
    }
    return best;
}
} // namespace doom3d
