/* Bullet impacts and explosions: wall and floor decals, scorches and
   debris. */
#include "internal.h"

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
void wallImpact(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz) {
    if(!line||gamestate!=GS_LEVEL)return;
    dropOldLevelImpacts();
    float x=units(fx),y=units(fy),z=units(fz);
    const sector_t *back=side?line->frontsector:line->backsector;
    int section=!back?0:z>=ceilZ(back)?1:z<=floorZ(back)?2:-1;
    WallSpan span;if(section<0||!wallSpan(*line,side,section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)return;
    float along=std::clamp(((x-span.a.x)*ex+(y-span.a.y)*ey)/len,0.0f,len);
    float now=gameSeconds();
    if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
    wallDecals.push_back({int(line-lines),side,section,along,z-span.anchor,now,randomRange(5.0f,7.5f),randomRange(0,1)<0.5f});
    if(span.texture<=0)return;
    Image &image=wallImage(texturetranslation[span.texture]);
    float light=lighting(sides[line->sidenum[side]].sector);
    float nx=ey/len,ny=-ex/len,tx=ex/len,ty=ey/len;
    float wx=span.a.x+tx*along+nx*1.5f,wy=span.a.y+ty*along+ny*1.5f;
    if(debris.size()>512)debris.erase(debris.begin(),debris.begin()+(debris.size()-512));
    for(int i=0;i<6;++i) {
        float out=randomRange(50,140),slide=randomRange(-55,55);
        debris.push_back({wx,wy,z,nx*out+tx*slide,ny*out+ty*slide,randomRange(20,120),now,randomRange(0.45f,0.8f),randomRange(1.2f,2.0f),texelColor(image),light});
    }
    shotWall(*line,side,section,along,x,y,z);
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
    const auto &puff=sprites[SPR_PUFF];
    if(puff.numframes>2) {
        int lump=firstspritelump+puff.spriteframes[2].lump[0];Image &image=lumpImage(lump,false);
        for(const auto &d:flatDecals) {
            const sector_t &sector=sectors[d.sector];
            if((d.ceiling?sector.ceilingpic:sector.floorpic)==skyflatnum)continue;
            if(d.ceiling?camera.eye[2]>ceilZ(&sector):camera.eye[2]<floorZ(&sector))continue; // Back of the flat.
            float fade=std::clamp((40-(now-d.born))/4,0.0f,1.0f)*0.6f;
            flatDecal(d.sector,d.x,d.y,d.size*0.5f,d.ceiling?ceilZ(&sector)-0.08f:floorZ(&sector)+0.08f,lump,image,fade,d.flip);
        }
        for(const auto &d:wallDecals) {
            WallSpan span;if(!wallSpan(lines[d.line],d.side,d.section,span))continue;
            float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
            if(ex*(camera.eye[1]-span.a.y)-ey*(camera.eye[0]-span.a.x)>0)continue; // Back of the wall.
            float zc=span.anchor+d.dz,h=d.size*0.5f;
            float u0=std::max(0.0f,d.along-h),u1=std::min(len,d.along+h),z0=std::max(span.bottom,zc-h),z1=std::min(span.top,zc+h);
            if(u1<=u0||z1<=z0)continue;
            float fade=std::clamp((40-(now-d.born))/4,0.0f,1.0f)*0.6f;
            float tx=ex/len,ty=ey/len,nx=ty*0.5f,ny=-tx*0.5f;
            auto texU=[&](float u){float t=(u-(d.along-h))/(2*h);return (d.flip?1-t:t)*image.width;};
            auto texV=[&](float z){return ((zc+h)-z)/(2*h)*image.height;};
            auto vertex=[&](float u,float z){return Vertex{span.a.x+tx*u+nx,span.a.y+ty*u+ny,z,texU(u),texV(z),fade,0};};
            quad(decalBatches[lump],vertex(u0,z1),vertex(u1,z1),vertex(u1,z0),vertex(u0,z0));
        }
    }
    debris.erase(std::remove_if(debris.begin(),debris.end(),[&](const Debris &p){return now-p.born>p.life||now<p.born;}),debris.end());
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
void impactsShutdown() {wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();}
} // namespace doom3d
