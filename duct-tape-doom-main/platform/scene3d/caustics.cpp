/* Caustic and shoreline maps around liquids, built at level load. */
#include "internal.h"

namespace doom3d {
// Caustics on walls near liquids: map cells hold the liquid's sector (r, and
// g's low 7 bits), whether it glows by itself (g's top bit: nukage, slime,
// lava), its tint at full brightness in 3-3-2 bits (b) and a proximity (a),
// so the shader can follow the light on the water as it changes. It spreads through open cells whose
// floors sit near the liquid surface, so ledges and closed doors stop it;
// void cells take their strongest neighbor for the walls on their boundary. The pattern
// is every animation frame of the level's most common liquid flat, stacked,
// keeping how far each texel (softened over its 3x3 neighbors, so lone bright
// texels drop out) rises above the flat's mean brightness, so the ripples are
// the WAD's own wave streaks and animate in step with the floor.
// Each frame has causticLayers rows of 64x64: 0 that plain pattern; 1-4 real
// caustics at causticHeights above the water, from reading the softened
// brightness as wave height and bouncing overhead light off it (8x8 rays per
// texel, splatted where they land; denser landing is brighter); 5-6 the wave
// slope in x and y around 128. The wave strength comes from the flat's own
// curvature (its 70th percentile) so the lines sharpen causticFocus units up;
// stored is how far the landing light exceeds the even spread, full at 4x.
constexpr float causticRange=96,causticFocus=40;
constexpr int causticLayers=7;
constexpr float causticHeights[4]={8,24,48,96};
std::vector<int> causticFrames;int causticSpeed=0;
void buildCaustics() {
    causticTexture=nullptr;causticPattern=nullptr;causticFrames.clear();causticSpeed=0;
    uint64_t start=SDL_GetTicksNS();
    size_t count=(size_t)mapWidth*mapHeight;
    std::vector<float> distance(count,causticRange),height(count,0);
    std::vector<int> source(count,-1);
    using Entry=std::pair<float,size_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    for(size_t i=0;i<count;++i) {
        int s=seamCells[i].own;
        if(s==noSector||!sectorMist[s].liquid||sectors[s].floorpic==skyflatnum)continue;
        distance[i]=0;height[i]=units(sectors[s].floorheight);source[i]=s;queue.push({0.0f,i});
    }
    if(queue.empty())return;
    size_t liquidCells=queue.size();
    while(!queue.empty()) {
        auto [d,i]=queue.top();queue.pop();
        if(d>distance[i])continue;
        int x=int(i%mapWidth),y=int(i/mapWidth);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            int nx=x+dx,ny=y+dy;
            if((!dx&&!dy)||nx<0||ny<0||nx>=mapWidth||ny>=mapHeight)continue;
            size_t n=(size_t)ny*mapWidth+nx;int s=seamCells[n].own;
            if(s==noSector)continue;
            float floor=units(sectors[s].floorheight);
            if(floor>height[i]+32||floor<height[i]-64||units(sectors[s].ceilingheight)-floor<8)continue;
            float next=d+mapCell*(dx&&dy?1.4142f:1.0f);
            if(next>=distance[n])continue;
            distance[n]=next;height[n]=height[i];source[n]=source[i];queue.push({next,n});
        }
    }
    std::vector<uint8_t> cells(count*4,0);
    for(size_t i=0;i<count;++i) {
        if(source[i]<0||source[i]>=32768)continue;
        const auto &mist=sectorMist[source[i]];const auto &color=mist.color;
        float peak=std::max({color[0],color[1],color[2],0.01f}),proximity=1-distance[i]/causticRange;
        auto level=[&](int c,int steps){return (int)std::lround(std::clamp(color[c]/peak,0.0f,1.0f)*steps);};
        cells[4*i]=(uint8_t)(source[i]&255);
        cells[4*i+1]=(uint8_t)((source[i]>>8)|(mist.toxic||mist.hot?128:0));
        cells[4*i+2]=(uint8_t)(level(0,7)<<5|level(1,7)<<2|level(2,3));
        cells[4*i+3]=(uint8_t)std::lround(proximity*proximity*255);
    }
    std::vector<uint8_t> dilated=cells;
    for(int y=0;y<mapHeight;++y)for(int x=0;x<mapWidth;++x) {
        size_t i=(size_t)y*mapWidth+x;if(seamCells[i].own!=noSector)continue;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=mapWidth||ny>=mapHeight)continue;
            size_t n=(size_t)ny*mapWidth+nx;
            if(seamCells[n].own!=noSector&&cells[4*n+3]>dilated[4*i+3])std::copy_n(&cells[4*n],4,&dilated[4*i]);
        }
    }
    std::map<int,int> flatUse;
    for(int i=0;i<numsectors;++i)if(sectorMist[i].liquid)++flatUse[sectors[i].floorpic];
    int pic=std::max_element(flatUse.begin(),flatUse.end(),[](const auto &a,const auto &b){return a.second<b.second;})->first,lump=firstflat+pic;
    causticFrames={pic};int next=pic;causticSpeed=P_PicAnimationNext(false,pic,&next);
    while(causticSpeed>0&&next!=pic&&causticFrames.size()<32) {causticFrames.push_back(next);P_PicAnimationNext(false,next,&next);}
    size_t frames=causticFrames.size();
    std::vector<byte> pattern;
    std::vector<float> luma(4096*frames,0);float mean=0,peak=0;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    for(size_t f=0;f<frames;++f) {
        int frameLump=firstflat+causticFrames[f];
        if(W_LumpLength(frameLump)<4096)continue;
        const byte *pixels=(const byte*)W_CacheLumpNum(frameLump,PU_CACHE);
        for(int i=0;i<4096;++i) {
            const byte *c=palette+pixels[i]*3;
            luma[f*4096+i]=0.299f*c[0]+0.587f*c[1]+0.114f*c[2];
        }
        std::array<float,4096> soft;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            float sum=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)sum+=luma[f*4096+((y+dy)&63)*64+((x+dx)&63)]*(dx||dy?1.0f:2.0f);
            soft[y*64+x]=sum/10;mean+=sum/10;peak=std::max(peak,sum/10);
        }
        std::copy(soft.begin(),soft.end(),luma.begin()+f*4096);
    }
    mean/=4096.0f*frames;
    std::vector<float> wave(luma.size()),slope(luma.size()*2),curvature;
    for(size_t i=0;i<luma.size();++i)wave[i]=(luma[i]-mean)/std::max(peak-mean,0.001f);
    float steepest=0.001f;
    for(size_t f=0;f<frames;++f) {
        auto h=[&](int x,int y){return wave[f*4096+(y&63)*64+(x&63)];};
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            size_t i=f*4096+y*64+x;
            slope[2*i]=(h(x+1,y)-h(x-1,y))/2;slope[2*i+1]=(h(x,y+1)-h(x,y-1))/2;
            steepest=std::max({steepest,std::abs(slope[2*i]),std::abs(slope[2*i+1])});
            float xx=h(x+1,y)-2*h(x,y)+h(x-1,y),yy=h(x,y+1)-2*h(x,y)+h(x,y-1);
            float xy=(h(x+1,y+1)-h(x-1,y+1)-h(x+1,y-1)+h(x-1,y-1))/4;
            curvature.push_back(std::abs((xx+yy)/2)+std::sqrt((xx-yy)*(xx-yy)/4+xy*xy));
        }
    }
    std::nth_element(curvature.begin(),curvature.begin()+curvature.size()*70/100,curvature.end());
    float bend=1/(causticFocus*std::max(curvature[curvature.size()*70/100],0.001f));
    pattern.assign(4096*frames*causticLayers,0);
    auto row=[&](int layer,size_t f){return pattern.data()+(layer*frames+f)*4096;};
    for(size_t f=0;f<frames;++f) {
        byte *plain=row(0,f);
        for(int i=0;i<4096;++i) {
            plain[i]=(byte)std::lround(std::clamp(wave[f*4096+i],0.0f,1.0f)*255);
            for(int axis=0;axis<2;++axis)
                row(5+axis,f)[i]=(byte)std::lround(128+127*std::clamp(slope[2*(f*4096+i)+axis]/steepest,-1.0f,1.0f));
        }
        // Bilinear slope between texel centers, so landing spots move smoothly.
        auto slopeAt=[&](float x,float y,int axis) {
            x-=0.5f;y-=0.5f;int x0=(int)std::floor(x),y0=(int)std::floor(y);float fx=x-x0,fy=y-y0;
            auto at=[&](int xx,int yy){return slope[2*(f*4096+(yy&63)*64+(xx&63))+axis];};
            return (at(x0,y0)*(1-fx)+at(x0+1,y0)*fx)*(1-fy)+(at(x0,y0+1)*(1-fx)+at(x0+1,y0+1)*fx)*fy;
        };
        for(int layer=0;layer<4;++layer) {
            float reach=bend*causticHeights[layer];
            std::array<float,4096> density={};
            for(int y=0;y<64;++y)for(int x=0;x<64;++x)for(int sy=0;sy<8;++sy)for(int sx=0;sx<8;++sx) {
                float px=x+(sx+0.5f)/8,py=y+(sy+0.5f)/8;
                float qx=px+reach*slopeAt(px,py,0)-0.5f,qy=py+reach*slopeAt(px,py,1)-0.5f;
                int x0=(int)std::floor(qx),y0=(int)std::floor(qy);float fx=qx-x0,fy=qy-y0;
                density[(y0&63)*64+(x0&63)]+=(1-fx)*(1-fy)/64;density[(y0&63)*64+((x0+1)&63)]+=fx*(1-fy)/64;
                density[((y0+1)&63)*64+(x0&63)]+=(1-fx)*fy/64;density[((y0+1)&63)*64+((x0+1)&63)]+=fx*fy/64;
            }
            byte *out=row(1+layer,f);
            for(int i=0;i<4096;++i)out[i]=(byte)std::lround(std::clamp((density[i]-1)/3,0.0f,1.0f)*255);
        }
    }
    causticTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,dilated.data(),mapWidth*4);
    causticPattern=gpuCreateTexture(GpuFormat::R8,64,64*(int)(frames*causticLayers),pattern.data(),64);
    if(!causticTexture||!causticPattern)I_Error((char*)"Could not allocate caustic maps");
    fprintf(stderr,"3D caustics: %zu liquid cells; pattern from %.8s, %zu frames, computed in %.0f ms.\n",liquidCells,lumpinfo[lump].name,frames,(SDL_GetTicksNS()-start)/1e6);
}
// Shorelines: where a liquid meets a wall, a closed door, a higher floor or
// dry ground at its own height (not another liquid, not a drop). Map cells
// hold the signed distance to the nearest such edge, positive on the
// liquid's side, as (d+shoreRange)/(2*shoreRange) in r; the shader samples it
// linearly, which is exact along straight edges, so damp banks keep their
// width however coarse the cells are. g and b's low 7 bits hold that edge's
// liquid sector, b's top bit marks cells with an edge in range, a its tint in
// 3-3-2 bits like causticMap. Lava and other hot liquids leave no damp.
constexpr float shoreRange=16;
void buildShore() {
    shoreTexture=nullptr;
    size_t count=(size_t)mapWidth*mapHeight;
    std::vector<float> best(count,1e9f);
    std::vector<int> liquidOf(count,-1);
    size_t edges=0;
    float reach=shoreRange+2*mapCell;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
        float ex=bx-ax,ey=by-ay,length2=ex*ex+ey*ey;if(length2<0.01f)continue;
        for(int side=0;side<2;++side) {
            const sector_t *wet=side?line.backsector:line.frontsector,*dry=side?line.frontsector:line.backsector;
            if(!wet||wet==dry)continue;
            int s=(int)(wet-sectors);const auto &mist=sectorMist[s];
            if(!mist.liquid||mist.hot||wet->floorpic==skyflatnum||s>=32768)continue;
            float water=units(wet->floorheight);
            if(dry) {
                float floor=units(dry->floorheight),ceiling=units(dry->ceilingheight);
                bool closed=ceiling-floor<8||ceiling<=water+8;
                bool flush=std::abs(floor-water)<=0.5f;
                if(!closed&&(floor<water-0.5f||(flush&&sectorMist[dry-sectors].liquid)))continue;
            }
            ++edges;
            // Doom's front side is on the line's right: negative cross product.
            float wetSign=side?1.0f:-1.0f;
            int x0=std::max(0,(int)std::floor((std::min(ax,bx)-reach-mapOrigin[0])/mapCell));
            int x1=std::min(mapWidth-1,(int)std::floor((std::max(ax,bx)+reach-mapOrigin[0])/mapCell));
            int y0=std::max(0,(int)std::floor((std::min(ay,by)-reach-mapOrigin[1])/mapCell));
            int y1=std::min(mapHeight-1,(int)std::floor((std::max(ay,by)+reach-mapOrigin[1])/mapCell));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell;
                float t=std::clamp(((px-ax)*ex+(py-ay)*ey)/length2,0.0f,1.0f);
                float d=std::hypot(px-ax-ex*t,py-ay-ey*t);
                size_t index=(size_t)y*mapWidth+x;
                if(d>=reach||d>=std::abs(best[index]))continue;
                float cross=ex*(py-ay)-ey*(px-ax);
                best[index]=cross*wetSign>=0?d:-d;liquidOf[index]=s;
            }
        }
    }
    if(!edges)return;
    std::vector<uint8_t> cells(count*4,0);
    for(size_t i=0;i<count;++i) {
        cells[4*i]=255;
        int s=liquidOf[i];if(s<0)continue;
        const auto &color=sectorMist[s].color;
        float peak=std::max({color[0],color[1],color[2],0.01f});
        auto level=[&](int c,int steps){return (int)std::lround(std::clamp(color[c]/peak,0.0f,1.0f)*steps);};
        cells[4*i]=(uint8_t)std::lround(std::clamp((best[i]+shoreRange)/(2*shoreRange),0.0f,1.0f)*255);
        cells[4*i+1]=(uint8_t)(s&255);
        cells[4*i+2]=(uint8_t)((s>>8)|128);
        cells[4*i+3]=(uint8_t)(level(0,7)<<5|level(1,7)<<2|level(2,3));
    }
    shoreTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,cells.data(),mapWidth*4);
    if(!shoreTexture)I_Error((char*)"Could not allocate the shore map");
    fprintf(stderr,"3D shorelines: %zu liquid edges.\n",edges);
}
void causticsShutdown() {causticTexture=nullptr;causticPattern=nullptr;shoreTexture=nullptr;}
} // namespace doom3d
