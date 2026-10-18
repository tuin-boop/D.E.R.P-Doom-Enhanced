/* Blood splats on floors and walls from hits, and pools under corpses. */
#include "internal.h"

namespace doom3d {
// Blood: decals drawn in the world pass and lit like the surface under them.
// Their artwork is generated per WAD from the BLUD sprite's own palette
// indices, ordered dark to bright by how often each is used: one pool shape
// and four splat shapes, one texel per unit like the flats.
constexpr int bloodKeyBase=-0x40000000; // Image keys clear of walls and lumps.
constexpr int bloodPool=0,bloodSplats=4;
static std::vector<byte> bloodShades;static bool bloodShadesLoaded=false;
static const std::vector<byte> &bloodPalette() {
    if(bloodShadesLoaded)return bloodShades;
    bloodShadesLoaded=true;
    if(SPR_BLUD>=numsprites)return bloodShades;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<std::pair<float,byte>> pixels;
    const auto &blood=sprites[SPR_BLUD];
    for(int frame=0;frame<blood.numframes;++frame) {
        const Image &image=lumpImage(firstspritelump+blood.spriteframes[frame].lump[0],false);
        for(size_t i=0;i+1<image.pixels.size();i+=2) if(image.pixels[i+1]) {
            const byte *rgb=palette+image.pixels[i]*3;
            pixels.push_back({0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2],image.pixels[i]});
        }
    }
    std::sort(pixels.begin(),pixels.end());
    for(const auto &pixel:pixels)bloodShades.push_back(pixel.second);
    return bloodShades;
}
static Image &bloodImage(int variant) {
    int key=bloodKeyBase+variant;
    auto found=images.find(key);if(found!=images.end())return found->second;
    const auto &shades=bloodPalette();
    bool pool=variant==bloodPool;
    int size=pool?64:32;float center=size*0.5f;
    std::vector<byte> pixels((size_t)size*size*2,0);
    uint32_t seed=hash32(variant*7919u+17u);
    float phase[3];for(int i=0;i<3;++i)phase[i]=hashUnit(i,variant,seed)*2*doomPi;
    auto put=[&](int x,int y,float shade) {
        if(x<0||y<0||x>=size||y>=size)return;
        size_t i=((size_t)y*size+x)*2;
        pixels[i]=shades[std::clamp((int)std::lround(std::clamp(shade,0.0f,1.0f)*(shades.size()-1)),0,(int)shades.size()-1)];
        pixels[i+1]=255;
    };
    for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        float dx=x+0.5f-center,dy=y+0.5f-center,d=std::hypot(dx,dy),a=std::atan2(dy,dx);
        float edge=pool?size*0.40f*(0.82f+0.10f*std::sin(2*a+phase[0])+0.06f*std::sin(5*a+phase[1])+0.04f*std::sin(9*a+phase[2]))
                       :size*0.30f*(0.70f+0.22f*std::sin(3*a+phase[0])+0.12f*std::sin(7*a+phase[1]));
        edge+=(valueNoise((float)x,(float)y,4,seed)-0.5f)*size*0.08f;
        if(d>=edge)continue;
        float rim=1-d/edge;
        put(x,y,pool?0.12f+0.30f*valueNoise((float)x,(float)y,6,seed^1u)+0.12f*(1-rim)
                    :0.25f+0.45f*std::pow(rim,0.6f)+0.2f*(valueNoise((float)x,(float)y,3,seed^2u)-0.5f));
    }
    // Droplets thrown past the edge.
    int drops=pool?3:6+int(hash32(seed)%5);
    for(int n=0;n<drops;++n) {
        float a=hashUnit(n,1,seed)*2*doomPi,reach=size*(pool?0.42f:0.30f+0.17f*hashUnit(n,2,seed));
        float radius=pool?1.5f+hashUnit(n,3,seed)*1.5f:0.8f+hashUnit(n,3,seed)*1.4f,shade=0.35f+0.25f*hashUnit(n,4,seed);
        float px=center+std::cos(a)*reach,py=center+std::sin(a)*reach;
        for(int y=(int)(py-radius);y<=(int)(py+radius)+1;++y)for(int x=(int)(px-radius);x<=(int)(px+radius)+1;++x)
            if(std::hypot(x+0.5f-px,y+0.5f-py)<radius)put(x,y,pool?shade*0.6f:shade);
    }
    return images.emplace(key,upload(size,size,pixels.data())).first->second;
}
struct BloodFloor { float x,y,born,size,angle;int sector,variant;bool flip; };
struct BloodWall { int line,side,section;float along,dz,born,size;int variant;bool flip; };
struct BloodPool { float x,y,born,size,angle;int sector; };
static std::vector<BloodFloor> bloodFloors;static std::vector<BloodWall> bloodWalls;static std::vector<BloodPool> bloodPools;
static std::unordered_map<const mobj_t*,bool> pooledCorpses;
static unsigned bloodLevelSerial;
static void checkBloodLevel() {
    if(bloodLevelSerial==r_levelserial)return;
    bloodFloors.clear();bloodWalls.clear();bloodPools.clear();pooledCorpses.clear();bloodLevelSerial=r_levelserial;
}
// Hitscan blood: splats land on the floor a little past the hit after their
// fall, and with harder hits the spray reaches a wall close behind.
void bloodHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage) {
    shotThrough(target,fx,fy,fz,fdx,fdy,damage);
    if(!settings.blood||gamestate!=GS_LEVEL||floors.empty()||bloodPalette().empty())return;
    checkBloodLevel();
    float x=units(fx),y=units(fy),z=units(fz),dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<0.001f)return;
    dx/=length;dy/=length;
    float now=gameSeconds();
    int count=1+(randomRange(0,30)<damage);
    for(int i=0;i<count;++i) {
        float reach=randomRange(4,36),side=randomRange(-10,10);
        float px=x+dx*reach-dy*side,py=y+dy*reach+dx*side,sx=px-x,sy=py-y,span=std::hypot(sx,sy);
        WallHit wall;
        if(span>0.5f&&firstSolidWall(x,y,z,sx/span,sy/span,0,span,wall))continue;
        const sector_t *sector=R_PointInSubsector((fixed_t)(px*FRACUNIT),(fixed_t)(py*FRACUNIT))->sector;
        float drop=z-floorZ(sector);
        if(sector->floorpic==skyflatnum||drop<0||drop>160)continue;
        float fall=(70+std::sqrt(4900+2450*drop))/1225; // Doom gravity after the 2 unit/tic hop.
        if(bloodFloors.size()>=160)bloodFloors.erase(bloodFloors.begin());
        bloodFloors.push_back({px,py,now+fall,randomRange(14,24)*(damage>=15?1.25f:1.0f),randomRange(0,2*doomPi),
                               int(sector-sectors),1+int(randomRange(0,bloodSplats))%bloodSplats,randomRange(0,1)<0.5f});
    }
    if(randomRange(0,1)>=std::min(0.75f,damage/16.0f))return;
    float sz=z+randomRange(-10,6);
    WallHit hit;WallSpan span;
    if(!firstSolidWall(x,y,sz,dx,dy,0,96,hit)||!wallSpan(lines[hit.line],hit.side,hit.section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)return;
    float along=std::clamp(((x+dx*hit.t-span.a.x)*ex+(y+dy*hit.t-span.a.y)*ey)/len,0.0f,len);
    if(bloodWalls.size()>=96)bloodWalls.erase(bloodWalls.begin());
    bloodWalls.push_back({hit.line,hit.side,hit.section,along,sz-span.anchor,now+hit.t/400,randomRange(16,26),
                          1+int(randomRange(0,bloodSplats))%bloodSplats,randomRange(0,1)<0.5f});
}
// A rotated square of blood clipped to its sector's floor polygons.
static void floorBlood(int variant,int sectorIndex,float cx,float cy,float size,float angle,bool flip,float wet,float dark,const Uniforms &camera) {
    const sector_t &sector=sectors[sectorIndex];
    if(sector.floorpic==skyflatnum)return;
    const Image &image=bloodImage(variant);
    float h=size*0.5f,c=std::cos(angle),s=std::sin(angle),reach=h*1.4143f;
    Point across={c,s},up={-s,c};
    auto corner=[&](float u,float v){return Point{cx+across.x*u+up.x*v,cy+across.y*u+up.y*v};};
    std::vector<Point> square={corner(-h,-h),corner(h,-h),corner(h,h),corner(-h,h)};
    float z=floorZ(&sector)+decalLift(cx,cy,camera),light=lighting(&sector);
    unsigned mode=MODE_BLOOD|sectorMode(&sector);
    auto &out=batches[bloodKeyBase+variant];
    for(int leaf:sectorFloors[sectorIndex]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<cx-reach||b[0]>cx+reach||b[3]<cy-reach||b[1]>cy+reach)continue;
        auto polygon=floors[leaf];
        for(size_t n=0;n<square.size()&&!polygon.empty();++n) {
            Point a=square[n],d=square[(n+1)%square.size()];
            polygon=clip(polygon,a,{d.x-a.x,d.y-a.y},false);
        }
        auto vertex=[&](Point p) {
            float u=((p.x-cx)*across.x+(p.y-cy)*across.y+h)/size,v=(h-((p.x-cx)*up.x+(p.y-cy)*up.y))/size;
            return Vertex{p.x,p.y,z,(flip?1-u:u)*image.width,v*image.height,light,mode,wet,dark,0};
        };
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]})out.push_back(vertex(p));
    }
}
// Wet blood steps from glossy to matte, darkening as it dries; values move
// in quarters and eighths at tic-rate ages, never smoothly.
static void bloodAge(float age,float wetFor,float dryOver,float &wet,float &dark) {
    float fresh=std::clamp(1-(age-wetFor)/dryOver,0.0f,1.0f);
    wet=settings.bloodShine?std::ceil(fresh*4)/4:0;
    dark=1-std::round((1-fresh)*0.28f*8)/8;
}
void buildBlood(const Uniforms &camera) {
    if(!settings.blood||bloodPalette().empty())return;
    checkBloodLevel();
    float now=gameSeconds();
    // Pools: corpses that came to rest, once each, sized to the body.
    for(auto &entry:pooledCorpses)entry.second=false;
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!(thing->flags&MF_CORPSE)||(thing->flags&MF_NOBLOOD))continue;
        auto seen=pooledCorpses.find(thing);
        if(seen!=pooledCorpses.end()) {seen->second=true;continue;}
        if(thing->z>thing->floorz||thing->momx||thing->momy||sectors[i].floorpic==skyflatnum)continue;
        pooledCorpses[thing]=true;
        if(bloodPools.size()>=64)bloodPools.erase(bloodPools.begin());
        bloodPools.push_back({units(thing->x),units(thing->y),now,std::clamp(units(thing->radius)*2.4f,28.0f,64.0f),randomRange(0,2*doomPi),i});
    }
    for(auto it=pooledCorpses.begin();it!=pooledCorpses.end();) it=it->second?std::next(it):pooledCorpses.erase(it);
    for(const auto &pool:bloodPools) {
        float age=now-pool.born;
        // The pool spreads over four seconds in eight steps.
        float grow=0.3f+0.7f*std::min(1.0f,std::floor(age*2)/8);
        float wet,dark;bloodAge(age,12,18,wet,dark);
        floorBlood(bloodPool,pool.sector,pool.x,pool.y,pool.size*grow,pool.angle,false,wet,dark,camera);
    }
    bloodFloors.erase(std::remove_if(bloodFloors.begin(),bloodFloors.end(),[&](const BloodFloor &d){return now-d.born>180;}),bloodFloors.end());
    for(const auto &d:bloodFloors) {
        if(now<d.born)continue;
        float wet,dark;bloodAge(now-d.born,0,8,wet,dark);
        floorBlood(d.variant,d.sector,d.x,d.y,d.size,d.angle,d.flip,wet,dark,camera);
    }
    // Wall splats stay matte; thin spray doesn't glint believably.
    bloodWalls.erase(std::remove_if(bloodWalls.begin(),bloodWalls.end(),[&](const BloodWall &d){return now-d.born>180;}),bloodWalls.end());
    for(const auto &d:bloodWalls) {
        if(now<d.born)continue;
        WallSpan span;if(!wallSpan(lines[d.line],d.side,d.section,span))continue;
        float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
        if(len<1||ex*(camera.eye[1]-span.a.y)-ey*(camera.eye[0]-span.a.x)>0)continue; // Back of the wall.
        float zc=span.anchor+d.dz,h=d.size*0.5f;
        float u0=std::max(0.0f,d.along-h),u1=std::min(len,d.along+h),z0=std::max(span.bottom,zc-h),z1=std::min(span.top,zc+h);
        if(u1<=u0||z1<=z0)continue;
        float wet,dark;bloodAge(now-d.born,0,8,wet,dark);
        const Image &image=bloodImage(d.variant);
        const line_t &line=lines[d.line];const sector_t *front=sides[line.sidenum[d.side]].sector;
        float light=lighting(front);
        if(std::abs(ey)<0.01f)light=std::max(0.1f,light-0.04f);
        if(std::abs(ex)<0.01f)light=std::min(1.0f,light+0.04f);
        float tx=ex/len,ty=ey/len,lift=decalLift(span.a.x+tx*d.along,span.a.y+ty*d.along,camera)+0.25f,nx=ty*lift,ny=-tx*lift;
        unsigned mode=MODE_BLOOD|sectorMode(front);
        auto vertex=[&](float u,float z) {
            float t=(u-(d.along-h))/(2*h);
            Vertex v={span.a.x+tx*u+nx,span.a.y+ty*u+ny,z,(d.flip?1-t:t)*image.width,((zc+h)-z)/(2*h)*image.height,light,mode,0,dark,0};
            bakeWallCoordinates(line,d.side,u,v);
            return v;
        };
        quad(batches[bloodKeyBase+d.variant],vertex(u0,z1),vertex(u1,z1),vertex(u1,z0),vertex(u0,z0));
    }
}
void bloodShutdown() {
    bloodFloors.clear();bloodWalls.clear();bloodPools.clear();pooledCorpses.clear();bloodShades.clear();bloodShadesLoaded=false;
}
} // namespace doom3d
