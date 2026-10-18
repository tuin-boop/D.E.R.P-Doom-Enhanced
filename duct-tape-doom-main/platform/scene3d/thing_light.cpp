/* Static light as things and sprites take it: traced from the baked
   lights, or from a grid baked at level load. */
#include "internal.h"

namespace doom3d {
int staticWidth=0,staticHeight=0;
unsigned staticSerial=0;
std::vector<StaticCell> staticGrid;
static StaticSample traceStatic(float x,float y,float z,int sector,const BakeMap &map=bakeMap) {
    StaticSample sample;
    if(bakeGrid.empty())return sample;
    float groups[flickerGroups]={},window[3]={},windowTowards[3]={},windowAmount=0;
    int points=(int)bakeSources.size();
    for(int n:bakeGridLights(x,y)) {
        float towards[3];
        const float *color=n<points?bakeSources[n].color:bakeAreas[n-points].color;
        bool isWindow=n>=firstWindowSource&&n<points;
        float amount=n<points?addBakedLight(map,bakeSources[n],sector,x,y,z,nullptr,isWindow?window:sample.color.data(),towards)
                             :addBakedArea(map,bakeAreas[n-points],sector,x,y,z,nullptr,sample.color.data(),towards);
        if(amount<=0)continue;
        float weight=amount*luminance(color);
        if(isWindow) {for(int c=0;c<3;++c)windowTowards[c]+=towards[c]*weight;windowAmount+=weight;continue;}
        for(int c=0;c<3;++c)sample.towards[c]+=towards[c]*weight;
        sample.amount+=weight;
        if(n>=points)continue;
        groups[sourceGroups[n]]+=weight;
        if(bakeSources[n].z>z-24)sample.shadows.add(n,amount);
    }
    float scale=capWindowLight(window);
    for(int c=0;c<3;++c) {sample.color[c]+=window[c];sample.towards[c]+=windowTowards[c]*scale;}
    sample.amount+=windowAmount*scale;
    for(int g=1;g<flickerGroups;++g)if(groups[g]>0&&groups[g]>(sample.group?groups[sample.group]:0))sample.group=g;
    if(sample.group)sample.groupShare=groups[sample.group]/sample.amount;
    return sample;
}
// Exact point samples: stationary actors need not trace unchanged static
// lights again. Private, bounded caches avoid locks between frame workers.
// Only the raw sample is retained; flicker, bounce and sector dimming stay live.
static uint64_t sampleRevision=1;
void invalidateStaticSamples() {++sampleRevision;}
static StaticSample cachedStatic(float x,float y,float z,int sector) {
    struct Entry {uint64_t revision=0;std::array<uint32_t,4> key{};StaticSample sample;};
    thread_local std::vector<Entry> cache(4096);
    std::array<uint32_t,4> key;memcpy(&key[0],&x,4);memcpy(&key[1],&y,4);memcpy(&key[2],&z,4);key[3]=(uint32_t)sector;
    uint32_t hash=2166136261u;for(uint32_t word:key){hash^=word;hash*=16777619u;hash^=hash>>16;}
    auto &entry=cache[hash&(cache.size()-1)];
    if(entry.revision!=sampleRevision||entry.key!=key) {
        entry.sample=traceStatic(x,y,z,sector);entry.key=key;entry.revision=sampleRevision;
    } else if(M_CheckParm((char*)"-verifylightthreads")) {
        auto fresh=traceStatic(x,y,z,sector);
        if(memcmp(&fresh,&entry.sample,sizeof(fresh)))I_Error((char*)"Cached static sprite sample differs from tracing");
    }
    return entry.sample;
}
// Grid lighting (settings.gridSpriteLight): a StaticSample every 32 units,
// 32 units above the floor of the sector at the cell's center and, in rooms
// taller than 112, also 96 above it. Things blend the nearest cells instead
// of tracing rays every frame. Baked after each light bake (staticSerial);
// cells in void or in closed sectors (doors) hold none.
bool gridLightActive() {
    return settings.gridSpriteLight&&bakedLightsActive&&staticSerial==lightBakeSerial&&!staticGrid.empty();
}
StaticCell staticCellAt(const BakeMap &map,int gx,int gy) {
    StaticCell cell;
    float x=mapOrigin[0]+(gx+0.5f)*staticCell,y=mapOrigin[1]+(gy+0.5f)*staticCell;
    int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
    int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
    int sector=seamCells[(size_t)cy*mapWidth+cx].own;
    if(sector==noSector)return cell;
    const BakeSector &s=map.sectors[sector];
    if(s.ceiling-s.floor<16)return cell;
    cell.used=true;
    cell.low=s.floor+std::min(32.0f,(s.ceiling-s.floor)*0.5f);
    cell.at[0]=traceStatic(x,y,cell.low,sector,map);
    cell.high=cell.low;cell.at[1]=cell.at[0];
    if(s.ceiling-s.floor>112) {cell.high=s.floor+96;cell.at[1]=traceStatic(x,y,cell.high,sector,map);}
    return cell;
}
void bakeStaticGrid() {
    loadingStage(LoadStage::Grid);
    finishRelight();
    staticSerial=lightBakeSerial;staticGrid.clear();
    if(bakeGrid.empty()||seamCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    staticWidth=(int)std::ceil(mapWidth*mapCell/staticCell);staticHeight=(int)std::ceil(mapHeight*mapCell/staticCell);
    staticGrid.assign((size_t)staticWidth*staticHeight,{});
    std::atomic<size_t> used{0};
    parallelFor(staticHeight,[&](int gy) {
        for(int gx=0;gx<staticWidth;++gx) {
            StaticCell &cell=staticGrid[(size_t)gy*staticWidth+gx];
            cell=staticCellAt(bakeMap,gx,gy);
            if(cell.used)++used;
        }
    });
    fprintf(stderr,"3D light grid: %zu cells of %dx%d, baked in %.0f ms.\n",used.load(),staticWidth,staticHeight,(SDL_GetTicksNS()-start)/1e6);
}
// The grid's blend at (x,y,z): bilinear over used cells, between each
// cell's two heights. The flicker group is the heaviest cell's; the shadow
// light is the one with the most blended amount.
static StaticSample gridStatic(float x,float y,float z) {
    StaticSample result;
    float gx=(x-mapOrigin[0])/staticCell-0.5f,gy=(y-mapOrigin[1])/staticCell-0.5f;
    int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
    float total=0,heaviest=0;
    StaticSample parts[4];float weights[4]={};
    std::array<int,8*maxShadowLights> shadowIds;shadowIds.fill(-1);
    std::array<float,8*maxShadowLights> shadowAmounts{};int shadowCount=0;
    for(int k=0;k<4;++k) {
        int cx=x0+(k&1),cy=y0+(k>>1);
        if(cx<0||cy<0||cx>=staticWidth||cy>=staticHeight)continue;
        const StaticCell &cell=staticGrid[(size_t)cy*staticWidth+cx];
        if(!cell.used)continue;
        float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
        float t=cell.high>cell.low?std::clamp((z-cell.low)/(cell.high-cell.low),0.0f,1.0f):0;
        const StaticSample &a=cell.at[0],&b=cell.at[1];
        StaticSample &p=parts[k];
        for(int c=0;c<3;++c) {p.color[c]=a.color[c]+(b.color[c]-a.color[c])*t;p.towards[c]=a.towards[c]+(b.towards[c]-a.towards[c])*t;}
        p.amount=a.amount+(b.amount-a.amount)*t;
        const StaticSample &nearer=t<0.5f?a:b;
        p.group=nearer.group;p.groupShare=nearer.groupShare;
        // Blend by source identity at both heights. Choosing the nearest
        // height or the winning lamp makes shadows jump at cell boundaries.
        for(int height=0;height<2;++height)for(int slot=0;slot<maxShadowLights;++slot){
            const auto &shadows=cell.at[height].shadows;
            int id=shadows.ids[slot];float amount=shadows.amounts[slot]*w*(height?t:1-t);
            if(id<0||amount<=0)continue;
            int at=0;while(at<shadowCount&&shadowIds[at]!=id)++at;
            if(at==shadowCount)shadowIds[shadowCount++]=id;
            shadowAmounts[at]+=amount;
        }
        weights[k]=w;total+=w;
        if(w*p.amount>heaviest) {heaviest=w*p.amount;result.group=p.group;}
    }
    if(total<=0)return result;
    float share=0,groupWeight=0;
    for(int k=0;k<4;++k) {
        if(weights[k]<=0)continue;
        float w=weights[k]/total;const StaticSample &p=parts[k];
        for(int c=0;c<3;++c) {result.color[c]+=p.color[c]*w;result.towards[c]+=p.towards[c]*w;}
        result.amount+=p.amount*w;
        if(result.group&&p.group==result.group) {share+=p.groupShare*w;groupWeight+=w;}
    }
    if(groupWeight>0)result.groupShare=share/groupWeight;
    for(int n=0;n<shadowCount;++n)result.shadows.add(shadowIds[n],shadowAmounts[n]/total);
    return result;
}
// Baked light at a thing's center or the eye: the static lights (from the
// grid when it is on), flickered by group (groupFlicker), plus the bounce
// between the floor and ceiling below and above it. sample receives the
// static light's details.
std::array<float,3> bakedLightAt(float x,float y,float z,const sector_t *sector,StaticSample *sample) {
    StaticSample statics;
    if(bakedLightsActive)statics=gridLightActive()?gridStatic(x,y,z):cachedStatic(x,y,z,(int)(sector-sectors));
    std::array<float,3> result=statics.color;
    fillHeadroom(result.data(),lighting(sector));
    if(statics.group) {
        float flicker=1-statics.groupShare*(1-groupFlicker(statics.group));
        for(float &c:result)c*=flicker;
    }
    if(settings.bounce&&bounceKey>=0&&!flatLightCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
        int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        size_t cell=(size_t)cy*mapWidth+cx;
        for(int c=0;c<3;++c) {
            statics.bounce[c]=(flatCell(floorBounce,cell)[c]+flatCell(ceilingBounce,cell)[c])/255.0f*bounceStrength;
            result[c]+=statics.bounce[c];
        }
    }
    float combinedKept=bakeOverloadScale(result.data());
    for(float &c:result)c*=combinedKept;
    if(sample)*sample=statics;
    return result;
}
// The lamp color of the sector light at a sprite lit at that level (lampTint),
// packed for Vertex::lampTint; white without static light.
unsigned packedLampTint(const StaticSample &statics,float light) {
    float tint[3];lampTint(statics.color.data(),statics.bounce.data(),settings.bounce?settings.bounceTint:0,light,tint);
    unsigned packed=0xFF000000u;
    for(int c=0;c<3;++c)packed|=(unsigned)std::lround(std::clamp(tint[c],0.0f,1.0f)*255)<<(8*c);
    return packed;
}
// The static light that casts a thing's shadow, when bake-only lights or the
// grid stand in for the dynamic copies: above the thing's feet at z.
void staticShadows(const StaticSample &sample,float z,ShadowLights &lights) {
    if(!(bakeOnlyActive||gridLightActive()))return;
    for(int slot=0;slot<maxShadowLights;++slot){
        int id=sample.shadows.ids[slot];if(id<0)continue;
        const BakeLight &source=bakeSources[id];
        if(source.z<=z+8)continue;
        float amount=sample.shadows.amounts[slot]*groupFlicker(sourceGroups[id]);
        lights.add({source.x,source.y,source.z,amount});
    }
}
void clearStaticGrid() {invalidateStaticSamples();staticGrid.clear();}
} // namespace doom3d
