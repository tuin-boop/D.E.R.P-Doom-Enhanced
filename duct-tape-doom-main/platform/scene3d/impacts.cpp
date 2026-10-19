/* Bullet impacts and explosions: wall and floor decals, scorches and
   debris. */
#include "internal.h"
#include "p_digwall.h"

namespace doom3d {
// Hitscan wall impacts leave fading bullet marks and a short burst of debris
// tinted by the wall texture's average color. Render-only: no game state,
// own random numbers, cleared on level changes and loads.
struct WallDecal { int line,side,section; float along,dz,born,size; bool flip; };
struct Scorch { float x,y,born,size;int sector; };
static std::vector<WallDecal> wallDecals;
std::vector<FlatDecal> flatDecals;
std::vector<Debris> debris;
static std::vector<Scorch> scorches;
static unsigned decalLevelSerial;
std::minstd_rand impactRandom(1993);
// Impacts from a previous level go once the engine has moved on.
void dropOldLevelImpacts() {
    if(decalLevelSerial!=r_levelserial){wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();decalLevelSerial=r_levelserial;}
}
// A block size map units across (tall times as high), but never under one
// Doom pixel, set on the Doom-pixel grid at its depth: a pixel of an old
// sprite rather than a soft speck. The color is lit, then put in the palette.
// A resting block stands on the point instead of centering on it, so a floor
// does not swallow half of it. Given its unlit color, the flashlight beam
// lights it too.
void pixelBlock(std::vector<Vertex> &out,const Uniforms &camera,float x,float y,float z,float size,float tall,const std::array<float,3> &color,bool resting,
                const std::array<float,3> *albedo) {
    float d[3]={x-camera.eye[0],y-camera.eye[1],z-camera.eye[2]};
    float depth=d[0]*camera.forward[0]+d[1]*camera.forward[1]+d[2]*camera.forward[2];
    if(depth<4)return;
    float unit=depth/(100*camera.projection[1]),across=100*camera.projection[1]/camera.projection[0]; // Map units per Doom pixel; half the view in Doom pixels.
    float px=(d[0]*camera.right[0]+d[1]*camera.right[1]+d[2]*camera.right[2])/unit;
    float py=(d[0]*camera.up[0]+d[1]*camera.up[1]+d[2]*camera.up[2])/unit;
    float w=std::max(1.0f,std::round(size/unit)),h=std::max(1.0f,std::round(size*tall/unit));
    float left=std::round(px+across-w*0.5f)-across,bottom=std::round(py+100-(resting?0:h*0.5f))-100;
    std::array<float,3> shade=color;
    if(albedo&&flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0&&camera.effects[2]==0) {
        const Flash &beam=flashes.lights[0];
        float amount=flashAt(x,y,z,beam);
        if(amount>0)for(int c=0;c<3;++c)shade[c]=std::min(1.0f,shade[c]+(*albedo)[c]*amount*beam.color[c]);
    }
    auto lit=paletteColor(shade);
    auto vertex=[&](float sx,float sz) {
        float r=(sx-px)*unit,u=(sz-py)*unit;
        Vertex v={x+camera.right[0]*r+camera.up[0]*u,y+camera.right[1]*r+camera.up[1]*u,z+camera.right[2]*r+camera.up[2]*u,0,0,1,0};
        v.red=lit[0];v.green=lit[1];v.blue=lit[2];return v;
    };
    quad(out,vertex(left,bottom+h),vertex(left+w,bottom+h),vertex(left+w,bottom),vertex(left,bottom));
}
// Section 0 is a one-sided wall (anchored to its floor), 1 an upper wall
// (anchored to the back ceiling, e.g. a door face), 2 a lower wall.
bool wallSpan(const line_t &line,int side,int section,WallSpan &span) {
    if(line.sidenum[side]<0)return false;
    const side_t &s=sides[line.sidenum[side]];
    const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
    span.a={units((side?line.v2:line.v1)->x),units((side?line.v2:line.v1)->y)};
    span.b={units((side?line.v1:line.v2)->x),units((side?line.v1:line.v2)->y)};
    if(section==0) {if(back)return false;span.bottom=floorZ(front);span.top=ceilZ(front);span.anchor=span.bottom;span.texture=s.midtexture;}
    else if(!back) return false;
    else if(section==1) {span.bottom=ceilZ(back);span.top=ceilZ(front);span.anchor=span.bottom;span.texture=s.toptexture;}
    else {span.bottom=floorZ(front);span.top=floorZ(back);span.anchor=span.top;span.texture=s.bottomtexture;}
    return span.top>span.bottom;
}
#include "impact_particles.inc"
#include "fluid_effects.inc"

void wallImpact(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz) {
    if(!line||gamestate!=GS_LEVEL)return;
    dropOldLevelImpacts();
    float x=units(fx),y=units(fy),z=units(fz);
    const sector_t *back=side?line->frontsector:line->backsector;
    int section=!back?0:z>=ceilZ(back)?1:z<=floorZ(back)?2:-1;
    WallSpan span;if(section<0||!wallSpan(*line,side,section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)return;
    float along=std::clamp(((x-span.a.x)*ex+(y-span.a.y)*ey)/len,0.0f,len);
    float now=leveltime/(float)TICRATE;
    shotWall(*line,side,section,along,x,y,z);
    fluidBulletMark(int(line-lines),side,section,along,z-span.anchor);
    if(!settings.gunImpacts)return;
    if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
    wallDecals.push_back({int(line-lines),side,section,along,z-span.anchor,now,randomRange(5.0f,7.5f),randomRange(0,1)<0.5f});
    if(span.texture<=0)return;
    Image &image=wallImage(texturetranslation[span.texture]);
    float light=lighting(sides[line->sidenum[side]].sector);
    float nx=ey/len,ny=-ex/len,tx=ex/len,ty=ey/len;
    float wx=span.a.x+tx*along+nx*1.5f,wy=span.a.y+ty*along+ny*1.5f;
    uziImpact(wx+nx*2.5f,wy+ny*2.5f,z,nx,ny,0);
}
void flatImpactParticles(float x,float y,float z,bool ceiling,Image &image,float light) {
    uziImpact(x,y,z+(ceiling?-4.f:4.f),0,0,ceiling?-1.f:1.f);
}
// Called only after a door actually moves upward. Emit at the two jambs,
// never across the face; distance, cadence and a global budget bound the work.
void doorMotion(sector_t *sector) {
    if(!settings.doorSparks||gamestate!=GS_LEVEL||!players[displayplayer].mo)return;
    int index=int(sector-sectors);
    if((leveltime+index)%3)return;
    dropOldLevelImpacts();
    const mobj_t *eye=players[displayplayer].mo;
    float px=units(eye->x),py=units(eye->y),now=leveltime/(float)TICRATE;
    static int budgetTic=-1,budget=0;
    if(budgetTic!=leveltime){budgetTic=leveltime;budget=0;}
    for(int i=0;i<sector->linecount&&budget<24;++i) {
        line_t &line=*sector->lines[i];
        int side=line.backsector==sector?0:1;
        sector_t *front=side?line.backsector:line.frontsector;
        if(!front||line.sidenum[side]<0||sides[line.sidenum[side]].toptexture<=0||
           front->ceilingheight<=sector->ceilingheight)continue;
        float ax=units((side?line.v2:line.v1)->x),ay=units((side?line.v2:line.v1)->y);
        float bx=units((side?line.v1:line.v2)->x),by=units((side?line.v1:line.v2)->y);
        float dx=bx-ax,dy=by-ay,len=std::hypot(dx,dy);if(len<8)continue;
        if(dx*(py-ay)-dy*(px-ax)>0)continue;
        float nx=dy/len,ny=-dx/len;
        for(int end=0;end<2&&budget<24;++end) {
            float f=end?1-2/len:2/len;
            float x=ax+dx*f+nx*2,y=ay+dy*f+ny*2;
            if(std::hypot(x-px,y-py)>640)continue;
            for(int n=0;n<2&&debris.size()<512;++n) {
                float out=randomRange(15,45),slide=randomRange(-24,24);
                debris.push_back({x,y,units(sector->ceilingheight)+1,
                    nx*out+dx/len*slide,ny*out+dy/len*slide,randomRange(-12,38),
                    now,randomRange(.16f,.3f),randomRange(.7f,1.2f),{1,.65f,.18f},1});
                ++budget;
            }
        }
    }
}
void doorDamage(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz,int broken) {
    if(!line)return;
    dropOldLevelImpacts();
    WallSpan span;if(!wallSpan(*line,side,1,span))return;
    float dx=span.b.x-span.a.x,dy=span.b.y-span.a.y,len=std::hypot(dx,dy);if(len<1)return;
    float along=std::clamp(((units(fx)-span.a.x)*dx+(units(fy)-span.a.y)*dy)/len,0.0f,len);
    float nx=dy/len,ny=-dx/len,x=span.a.x+dx/len*along+nx*2,y=span.a.y+dy/len*along+ny*2;
    float z=std::clamp(units(fz),span.bottom,span.top),now=leveltime/(float)TICRATE;
    if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
    wallDecals.push_back({int(line-lines),side,1,along,z-span.anchor,now,broken?64.f:36.f,false});
    int count=broken?32:12;
    if(debris.size()+count>512)debris.erase(debris.begin(),debris.begin()+(debris.size()+count-512));
    for(int i=0;i<count;++i) {
        float out=randomRange(60,220),slide=randomRange(-140,140);
        std::array<float,3> color={1,.65f,.18f};
        if(broken&&span.texture>0)color=texelColor(wallImage(texturetranslation[span.texture]));
        float u=broken?randomRange(2,std::max(2.f,len-2)):along;
        float sx=span.a.x+dx/len*u+nx*2,sy=span.a.y+dy/len*u+ny*2;
        float sz=broken?randomRange(span.bottom,span.top):z;
        debris.push_back({sx,sy,sz,nx*out+dx/len*slide,ny*out+dy/len*slide,randomRange(40,160),now,
            broken?1.2f:.4f,broken?randomRange(1.5f,3.f):1.f,color,broken?lighting(sides[line->sidenum[side]].sector):1.f});
    }
}
void barrelBlast(const mobj_t &thing,float x,float y,float z) {
    dropOldLevelImpacts();
    glassBlast(x,y,z+24,128);
    blastLights(x,y,z+24,128);
    float now=gameSeconds();
    if(scorches.size()>=48)scorches.erase(scorches.begin());
    scorches.push_back({x,y,now,randomRange(60,76),int(thing.subsector->sector-sectors)});
    if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
    for(int i=0;i<32;++i) {
        float angle=randomRange(0,2*doomPi),speed=randomRange(60,220),heat=randomRange(0,1);
        debris.push_back({x,y,z+randomRange(8,32),std::cos(angle)*speed,std::sin(angle)*speed,randomRange(80,260),now,
            randomRange(0.6f,1.3f),randomRange(1.0f,2.2f),{1.0f,0.45f+0.5f*heat,0.08f+0.3f*heat*heat}});
    }
}
// A square decal on a sector's floor or ceiling, clipped to its leaves.
static void flatDecal(int sectorIndex,float x,float y,float h,float z,int lump,const Image &image,float fade,bool flip) {
    std::vector<Point> square={{x-h,y-h},{x+h,y-h},{x+h,y+h},{x-h,y+h}};
    for(int leaf:sectorFloors[sectorIndex]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<x-h||b[0]>x+h||b[3]<y-h||b[1]>y+h)continue;
        auto polygon=floors[leaf];
        for(size_t n=0;n<square.size()&&!polygon.empty();++n) {
            Point a=square[n],c=square[(n+1)%square.size()];
            polygon=clip(polygon,a,{c.x-a.x,c.y-a.y},false);
        }
        auto vertex=[&](Point p){float u=(p.x-x+h)/(2*h);return Vertex{p.x,p.y,z,(flip?1-u:u)*image.width,(y+h-p.y)/(2*h)*image.height,fade,0};};
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]})decalBatches[lump].push_back(vertex(p));
    }
}
void buildImpacts(const Uniforms &camera) {
    for(auto &batch:decalBatches)batch.second.clear();
    particleVertices.clear();
    dropOldLevelImpacts();
    float now=gameSeconds();
    scorches.erase(std::remove_if(scorches.begin(),scorches.end(),[&](const Scorch &d){return now-d.born>60||now<d.born;}),scorches.end());
    const auto &blast=sprites[SPR_BEXP];
    if(blast.numframes>2&&!scorches.empty()) {
        // Scorches reuse a fireball frame's coverage, clipped to the sector floor.
        int lump=firstspritelump+blast.spriteframes[2].lump[0];Image &image=lumpImage(lump,false);
        for(const auto &d:scorches) {
            const sector_t &sector=sectors[d.sector];if(sector.floorpic==skyflatnum)continue;
            float fade=std::clamp((60-(now-d.born))/8,0.0f,1.0f)*0.7f;
            flatDecal(d.sector,d.x,d.y,d.size*0.5f,floorZ(&sector)+0.08f,lump,image,fade,false);
        }
    }
    wallDecals.erase(std::remove_if(wallDecals.begin(),wallDecals.end(),[&](const WallDecal &d){return now-d.born>40||now<d.born;}),wallDecals.end());
    flatDecals.erase(std::remove_if(flatDecals.begin(),flatDecals.end(),[&](const FlatDecal &d){return now-d.born>40||now<d.born;}),flatDecals.end());
    // Do not reuse Doom's grey PUFF sprite as a persistent impact decal.
    // The imported Uzi flash and voxel dust are the complete surface effect.
    debris.erase(std::remove_if(debris.begin(),debris.end(),[&](const Debris &p){return now-p.born>p.life||now<p.born;}),debris.end());
    drawImpactParticles(camera);
    drawFluidEffects(camera);
    // Debris moves at the tic rate, like the rings, stretches upright while
    // it falls fast and shrinks once before it is gone.
    for(const auto &p:debris) {
        float t=std::floor((now-p.born)*TICRATE)/TICRATE,x=p.x+p.vx*t,y=p.y+p.vy*t,z=p.z+p.vz*t-210*t*t;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        float floor=floorZ(sector)+0.2f;
        size_t index=sector-sectors;
        if(z<floor&&index<sectorMist.size()&&sectorMist[index].liquid&&!sectorMist[index].thick)continue; // Back into the water.
        z=std::min(z,ceilZ(sector)-p.size*0.5f);
        float size=t>p.life*0.75f?p.size*0.5f:p.size,tall=std::abs(p.vz-420*t)>220?2.0f:1.0f;
        std::array<float,3> lit;for(int c=0;c<3;++c)lit[c]=std::min(1.0f,p.color[c]*p.light);
        pixelBlock(particleVertices,camera,x,y,std::max(z,floor),size,tall,lit,z<=floor+p.size*0.5f,&p.color);
    }
}
// The first wall a ray from (x,y,z) along the unit horizontal (dx,dy) meets
// within length, climbing dz per unit: one-sided lines, and two-sided ones
// where the ray passes above the far ceiling or below the far floor, at the
// current heights. side faces the ray's origin; section is as in wallSpan.
bool firstSolidWall(float x,float y,float z,float dx,float dy,float dz,float length,WallHit &hit) {
    std::vector<std::pair<float,int>> crossings;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        float det=dx*ey-dy*ex;if(std::abs(det)<1e-6f)continue;
        float ox=ax-x,oy=ay-y,t=(ox*ey-oy*ex)/det,u=(ox*dy-oy*dx)/det;
        if(t>0.01f&&t<length&&u>=0&&u<=1)crossings.push_back({t,i});
    }
    std::sort(crossings.begin(),crossings.end());
    for(auto [t,i]:crossings) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        int side=ex*(y-ay)-ey*(x-ax)>=0?1:0;
        const sector_t *front=side?line.backsector:line.frontsector,*back=side?line.frontsector:line.backsector;
        if(!front)continue;
        float at=z+dz*t;int section=-1;
        if(!back)section=0;
        else if(at>=ceilZ(back)&&!(front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum))section=1;
        else if(at<=floorZ(back))section=2;
        if(section<0)continue;
        hit={i,side,section,t};return true;
    }
    return false;
}
float valueNoise(float x,float y,float cell,uint32_t seed) {
    x/=cell;y/=cell;int ix=(int)std::floor(x),iy=(int)std::floor(y);
    float fx=x-ix,fy=y-iy;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    float a=hashUnit(ix,iy,seed),b=hashUnit(ix+1,iy,seed),c=hashUnit(ix,iy+1,seed),d=hashUnit(ix+1,iy+1,seed);
    float top=a+(b-a)*fx,bottom=c+(d-c)*fx;
    return top+(bottom-top)*fy;
}
void impactsShutdown() {fluidParticles.clear();fluidBursts.clear();fluidMarks.clear();wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();impactParticles.clear();gunPuffs.clear();}
} // namespace doom3d

#ifdef DOOM_TEST_DRIVER
#include "../settings_panel.h"
extern "C" void I_TestImpactOptions() {
    using namespace doom3d;
    // A solid boundary must reflect debris; an open doorway must admit it.
    {sector_t fixture[2]={};sector_t *original=sectors;
     vertex_t a={},b={};a.x=b.x=FRACUNIT;a.y=-10*FRACUNIT;b.y=10*FRACUNIT;
     line_t edge={};edge.v1=&a;edge.v2=&b;edge.dy=20*FRACUNIT;edge.frontsector=&fixture[0];
     line_t *boundary=&edge;for(auto &sec:fixture){sec.ceilingheight=64*FRACUNIT;sec.linecount=1;sec.lines=&boundary;}
     sectors=fixture;
     ImpactParticle p={};p.z=p.oz=32;p.vx=100;p.life=1;
     moveImpactParticle(p,.02f);
     if(p.x>=1||p.vx>=0)I_Error("Impact fragment crossed solid wall");
     p={};p.z=32;p.vx=100;p.life=1;edge.backsector=&fixture[1];
     moveImpactParticle(p,.02f);
     if(p.sector!=1||p.x<1.9f)I_Error("Impact fragment did not cross open portal");
     p={};p.z=32;p.vx=100;p.life=1;fixture[1].ceilingheight=16*FRACUNIT;
     moveImpactParticle(p,.02f);
     if(p.x>=1||p.vx>=0)I_Error("Impact fragment crossed closed door");
     sectors=original;}
    for(const auto &mask:fluidMasks){bool clear=false,covered=false;for(int i=0;i<mask.w*mask.h;++i){clear|=mask.alpha[i]==0;covered|=mask.alpha[i]>0;}if(!clear||!covered)I_Error("Fluid mask lost transparency");}
    resetFluidEffects();
    auto saveFluidParticles=fluidParticles;auto saveFluidMarks=fluidMarks;auto saveFluidBursts=fluidBursts;
    int enabled=settings.fluidWeapons;
    fluidParticles.clear();fluidMarks.clear();fluidBursts.clear();settings.fluidWeapons=1;
    for(auto type:{MT_ROCKET,MT_PLASMA,MT_BFG}){
        mobj_t missile=*players[displayplayer].mo;missile.type=type;missile.target=players[displayplayer].mo;
        missile.flags=MF_MISSILE;missile.momx=20*FRACUNIT;missile.momy=0;missile.momz=8*FRACUNIT;
        missile.z=missile.floorz;missile.info=&mobjinfo[type];
        size_t count=fluidParticles.size();fluidProjectileFlight(missile);
        if(fluidParticles.size()<=count)I_Error("Missing Fluid projectile trail");
        size_t marks=fluidMarks.size();fluidProjectileImpact(&missile);
        if(fluidBursts.empty())I_Error("Missing Fluid projectile impact");
        if(missile.subsector->sector->floorpic!=skyflatnum&&fluidMarks.size()<=marks)I_Error("Missing Fluid projectile scorch");
    }
    for(int i=0;i<80;++i)tickFluidEffects();
    if(!fluidParticles.empty()||!fluidBursts.empty())I_Error("Fluid projectile effects failed to expire");
    // Exercise a real map corner: the next face must share the same UV origin.
    bool wrapped=false;
    for(int i=0;i<numlines&&!wrapped;++i)for(int side=0;side<2&&!wrapped;++side)for(int part=0;part<3&&!wrapped;++part){
        WallSpan span;if(!wallSpan(lines[i],side,part,span)||span.texture<=0)continue;
        fluidMarks.clear();float len=std::hypot(span.b.x-span.a.x,span.b.y-span.a.y);
        addFluidMark(i,side,part,-1,false,len-1,(span.top+span.bottom)*.5f-span.anchor,5,.5f);
        if(fluidMarks.size()>1){wrapped=true;const auto first=fluidMarks.front();
            for(const auto &m:fluidMarks)if(m.flip!=first.flip||m.born!=first.born||m.mask!=first.mask)I_Error("Corner decal continuity lost");
        }
    }
    if(!wrapped)I_Error("Fluid decals failed to wrap map corners");
    fluidParticles.clear();fluidBursts.clear();fluidBursts.push_back({0,0,0,MT_PLASMA,0});
    tickFluidEffects();tickFluidEffects();
    if(fluidParticles.size()>10)I_Error("Plasma impact emitted lightning instead of sparks");
    settings.fluidWeapons=0;tickFluidEffects();if(!fluidMarks.empty())I_Error("Disabled Fluid effects left decals");
    settings.fluidWeapons=enabled;
    fluidParticles=std::move(saveFluidParticles);fluidMarks=std::move(saveFluidMarks);fluidBursts=std::move(saveFluidBursts);
    puts("Fluid rocket/plasma/BFG trails, impacts, scorches, expiry and toggle passed.");
    int optionCount=0;
    Settings config;
    for(auto &tab:optionTabs(config))for(auto &section:tab.sections)for(auto &option:section.options)
        if(!strcmp(option.key,"door_sparks")||
           !strcmp(option.key,"gun_impacts")) {
            if(strcmp(tab.title,"Effects"))I_Error("Impact/door option is outside Effects tab");
            ++optionCount;
        }
    if(optionCount!=2)I_Error("Missing/duplicate Effects options");
    readOption(config,"gun_impacts",0);
    if(config.gunImpacts)I_Error("Impact config keys not loaded");
    dropOldLevelImpacts();
    auto savedGunPuffs=gunPuffs;gunPuffs.clear();
    auto savedParticles=impactParticles;impactParticles.clear();
    auto savedDebris=debris;auto savedWalls=wallDecals;auto savedScorches=scorches;
    int gun=settings.gunImpacts,lights=settings.shootableLights;
    auto random=impactRandom;settings.shootableLights=0;debris.clear();wallDecals.clear();scorches.clear();
    bool tested=false;
    for(int i=0;i<numlines&&!tested;++i) {
        line_t &l=lines[i];WallSpan span;
        if(!wallSpan(l,0,0,span)||span.texture<=0)continue;
        fixed_t x=(fixed_t)((span.a.x+span.b.x)*.5f*FRACUNIT);
        fixed_t y=(fixed_t)((span.a.y+span.b.y)*.5f*FRACUNIT);
        fixed_t z=(fixed_t)((span.bottom+span.top)*.5f*FRACUNIT);
        settings.gunImpacts=0;wallImpact(&l,0,x,y,z);
        if(!impactParticles.empty()||!wallDecals.empty())I_Error("Disabled gun impacts emitted effects");
        settings.gunImpacts=1;wallImpact(&l,0,x,y,z);
        if(impactParticles.size()!=6||wallDecals.size()!=1)I_Error("Enabled gun impact burst missing");
        mobj_t puff={};puff.type=MT_PUFF;puff.x=x;puff.y=y;puff.z=z;
        if(!customGunPuff(&puff))I_Error("Enhanced hit left the original puff visible");
        puff.z+=24*FRACUNIT; // Stock puff rises after spawning; keep it replaced.
        if(!customGunPuff(&puff))I_Error("Rising original puff escaped replacement");
        puff.x+=100*FRACUNIT;
        if(customGunPuff(&puff))I_Error("Unrelated actor puff was hidden");
        tested=true;
    }
    if(!tested)I_Error("No wall for gun impact regression");
    impactParticles.clear();debris.clear();wallDecals.clear();scorches.clear();
    for(int i=0;i<100;++i)uziImpact(units(players[displayplayer].mo->x),units(players[displayplayer].mo->y),32,0,0,1);
    if(impactParticles.size()>512)I_Error("Uzi effects exceeded particle budget");
    gunPuffs=std::move(savedGunPuffs);
    impactParticles=std::move(savedParticles);
    debris=std::move(savedDebris);wallDecals=std::move(savedWalls);scorches=std::move(savedScorches);
    settings.gunImpacts=gun;settings.shootableLights=lights;impactRandom=random;
    puts("Uzi impact toggles and particle budget passed.");
}

#endif
