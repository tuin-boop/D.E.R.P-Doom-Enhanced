/* Ripple rings, spouts and bubbles where things enter or move in or are
   shot into liquids. */
#include "internal.h"

namespace doom3d {
// Splashes: per tic, things in liquid sectors are watched for landing,
// projectiles exploding at the surface, wading, and bodies coming to rest
// in it (a corpse's last death frame). Each event spreads rings; shots and
// projectiles also raise a spout. The reflection stays as it is.
struct SplashWatch { float z;bool missile,seen;int ripple;bool down;float x,y; };
struct Ring { float x,y,born,speed,life,strength;int sector; };
struct Spout { float x,y,born;int sector;uint32_t seed; };
static std::unordered_map<const mobj_t*,SplashWatch> splashWatch;
static std::vector<Ring> rings;
static std::vector<Spout> spouts;
static unsigned splashLevelSerial;
// Rings and spouts hold each frame this many tics, like the bullet puff,
// so they step like a Doom sprite rather than moving every tic.
constexpr float frameTics=4;
// Water rings race out; thick liquids' rings spread slowly and not far.
static void ripple(float x,float y,int sector,float size,bool shot=false) {
    float now=leveltime/(float)TICRATE;
    bool thick=sectorMist[sector].thick;
    if(rings.size()>=24)rings.erase(rings.begin());
    rings.push_back({x,y,now,thick?10+size*0.2f:30+size*0.6f,thick?1.3f+size*0.015f:0.8f+size*0.01f,shot?1.0f:std::min(1.0f,size/40),sector});
}
// One spout per blast: pellets landing close together in the same tic share it.
static void spout(float x,float y,int sector) {
    float now=leveltime/(float)TICRATE;
    for(const Spout &s:spouts)if(s.born==now&&std::hypot(s.x-x,s.y-y)<32)return;
    if(spouts.size()>=16)spouts.erase(spouts.begin());
    spouts.push_back({x,y,now,sector,(uint32_t)leveltime*2654435761u^(uint32_t)(randomRange(0,1)*16777216)});
}
// Free-aimed hitscan shots stopping on a floor or ceiling (R_PlaneImpact):
// liquids splash, the pellets of one blast sharing a ring where they land
// close together; other flats take a bullet mark and chips in their color.
boolean planeImpact(sector_t *sector,boolean ceiling,fixed_t fx,fixed_t fy,fixed_t fz) {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return false;
    dropOldLevelImpacts();
    int index=int(sector-sectors);float x=units(fx),y=units(fy),z=units(fz),now=gameSeconds();
    if(!ceiling&&sectorMist[index].liquid) {
        if(!settings.splashes)return false;
        float tic=leveltime/(float)TICRATE; // As splash() stamps its rings.
        bool shared=std::any_of(rings.begin(),rings.end(),[&](const Ring &r){return r.born==tic&&std::hypot(r.x-x,r.y-y)<32;});
        if(!shared)ripple(x,y,index,16,true);
        spout(x,y,index);
        return true;
    }
    if(flatDecals.size()>=96)flatDecals.erase(flatDecals.begin());
    flatDecals.push_back({x,y,now,randomRange(5.0f,7.5f),index,(bool)ceiling,randomRange(0,1)<0.5f});
    int pic=ceiling?sector->ceilingpic:sector->floorpic;
    Image &image=lumpImage(firstflat+flattranslation[pic],true);
    float light=lighting(sector);
    if(debris.size()>512)debris.erase(debris.begin(),debris.begin()+(debris.size()-512));
    for(int i=0;i<5;++i) {
        float angle=randomRange(0,2*doomPi),out=randomRange(20,70);
        debris.push_back({x,y,ceiling?z-3:z+1,std::cos(angle)*out,std::sin(angle)*out,ceiling?randomRange(-40,0):randomRange(60,140),now,
            randomRange(0.45f,0.8f),randomRange(1.2f,2.0f),texelColor(image),light});
    }
    shotFlat(index,ceiling,x,y,z);
    return false;
}
void watchSplashes() {
    if(!settings.splashes||levelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return;
    if(splashLevelSerial!=r_levelserial) {splashWatch.clear();rings.clear();spouts.clear();splashLevelSerial=r_levelserial;}
    for(auto &entry:splashWatch)entry.second.seen=false;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].liquid||sectors[i].floorpic==skyflatnum)continue;
        float floor=units(sectors[i].floorheight);
        for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
            bool missile=thing->flags&MF_MISSILE;
            if((thing->flags&MF_NOBLOCKMAP)&&!missile)continue;
            float x=units(thing->x),y=units(thing->y),z=units(thing->z);
            bool surface=z<=floor+1;
            bool down=(thing->flags&MF_CORPSE)&&thing->tics==-1;
            auto found=splashWatch.find(thing);
            if(found==splashWatch.end()) {splashWatch[thing]={z,missile,true,leveltime,down,x,y};continue;}
            SplashWatch &watch=found->second;watch.seen=true;
            float size=units(thing->radius)*2;
            if(down&&!watch.down&&surface) {
                ripple(x,y,i,std::clamp(size*0.8f+16,24.0f,56.0f));
                watch.down=true;watch.ripple=leveltime;watch.z=z;watch.x=x;watch.y=y;continue;
            }
            watch.down=down;
            if(watch.missile&&!missile&&z<=floor+8) {ripple(x,y,i,48);spout(x,y,i);}
            else if(surface&&watch.z>floor+3) {
                ripple(x,y,i,std::clamp(size*0.5f+(watch.z-z)*0.8f,12.0f,64.0f));
                watch.ripple=leveltime;
            } else if(surface&&!down&&std::hypot(x-watch.x,y-watch.y)>1&&leveltime-watch.ripple>=12) {
                // Monsters step without momentum, so movement is the change
                // in position since the last tic.
                ripple(x,y,i,size*0.4f);watch.ripple=leveltime;
            }
            watch.z=z;watch.missile=missile;watch.x=x;watch.y=y;
        }
    }
    for(auto it=splashWatch.begin();it!=splashWatch.end();) it=it->second.seen?std::next(it):splashWatch.erase(it);
}
// The light a ring, spout or bubble takes at a point: its sector's, faded with
// distance like the floor it stands on (world.frag), so it reads as lit
// liquid, not a glow, in dark rooms. Glowing liquids stay at full light.
float liquidLight(int sector,Image &flat,float x,float y,float z,const Uniforms &camera) {
    if(flat.emissionCoverage>0.2f||sectorMist[sector].hot||camera.effects[2]!=0)return 1;
    float depth=(x-camera.eye[0])*camera.forward[0]+(y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
    return lighting(&sectors[sector])*std::clamp(1-depth/3200,0.35f,1.0f);
}
// Water's foam: its flat's colors lifted toward white, a paler blue that
// keeps the hue. Only as pale as the light it stands in: dimly lit, a pale
// blue falls on the palette's greys, so in the dark it stays the water's blue.
static std::array<float,3> foam(std::array<float,3> color,float lift,float light) {
    lift*=std::clamp(light,0.0f,1.0f);
    for(float &c:color)c+=(1-c)*lift;
    return color;
}
// Spouts: a drawn effect, not a simulation; nothing in it falls. A trunk
// fat at the surface and slimming as it shoots up forks halfway into a few
// branches, among loose specks that spread wider the higher they are. It
// grows in five tics on a foam base, then dissolves from the top while the
// trunk thins and the specks blink out, the high ones first. Thick liquids
// raise a lower, fatter, sparser spout that takes longer. Laid out across
// the view like a sprite and rebuilt each frame from the spout's seed;
// stepped by frameTics, colored from the liquid's own to its brightest texel.
static void buildSpouts(const Uniforms &camera) {
    float now=gameSeconds();
    spouts.erase(std::remove_if(spouts.begin(),spouts.end(),[&](const Spout &s){
        return now<s.born-0.1f||(now-s.born)*TICRATE>(sectorMist[s.sector].thick?36:22);}),spouts.end());
    float rx=camera.right[0],ry=camera.right[1],rl=std::hypot(rx,ry);
    if(rl<1e-3f){rx=1;ry=0;}else{rx/=rl;ry/=rl;}
    auto ease=[](float u){u=std::clamp(u,0.0f,1.0f);return 1-(1-u)*(1-u);};
    for(const Spout &s:spouts) {
        const sector_t &sector=sectors[s.sector];
        bool thick=sectorMist[s.sector].thick;
        float t=std::floor(std::max(0.0f,now-s.born)*TICRATE/(thick?1.6f:1)/frameTics)*frameTics,floor=floorZ(&sector)+0.2f,ceiling=ceilZ(&sector);
        float height=thick?24:40,base=thick?20:17,block=thick?3:2;
        Image &flat=lumpImage(firstflat+flattranslation[sector.floorpic],true);
        float light=liquidLight(s.sector,flat,s.x,s.y,floor+height*0.5f,camera);
        const auto &brightest=brightestColor(flat);const auto &color=sectorMist[s.sector].color;
        std::array<float,3> albedo[3]; // Dim edge, body, crest.
        for(int k=0;k<3;++k)for(int c=0;c<3;++c)albedo[k][c]=color[c]+(brightest[c]-color[c])*(k==0?0.25f:k==1?0.6f:1.0f);
        // Water foams paler toward the top.
        auto put=[&](float side,float depth,float z,float size,int shade) {
            z=std::min(z,ceiling-floor-size);
            std::array<float,3> shown=thick?albedo[shade]:foam(albedo[shade],0.3f+0.3f*std::clamp(z/height,0.0f,1.0f),light),lit;
            for(int c=0;c<3;++c)lit[c]=std::min(1.0f,shown[c]*light);
            pixelBlock(particleVertices,camera,s.x+rx*side+ry*depth,s.y+ry*side-rx*depth,floor+z,size,1,lit,true,&shown);
        };
        int draw=0;auto random=[&]{return hashUnit(draw++,0x5b0u,s.seed);};
        auto gauss=[&]{return (random()+random()+random()-1.5f)/1.5f;};
        float front=height*1.05f*ease((t+1)/5),collapse=height*1.1f*(1-std::max(0.0f,(t-8)/8)),slim=1-std::clamp((t-9)/10,0.0f,0.7f);
        if(t<12) {
            int n=std::max(2,(int)std::round((base+3)*(t<7?1:0.7f)/2));
            for(int i=0;i<n;++i)put((i-(n-1)*0.5f)*2,0,0,2,t<6?2:1);
        }
        float x=0,split=height*(0.45f+random()*0.15f);
        for(float z=0;z<split;z+=block) {
            x+=(random()-0.5f)*0.8f;
            if(z>front||z>=collapse)continue;
            float width=std::max(block,(block+(base-block)*std::pow(1-z/split,1.6f))*slim);
            int n=std::max(1,(int)std::round(width/block));
            for(int j=0;j<n;++j) {
                bool edge=n>2&&(j==0||j==n-1);
                put(x+(j-(n-1)*0.5f)*block*0.9f,edge?0.1f:0,z,block,edge?0:z<4?2:1);
            }
        }
        int branches=thick?2:3;
        for(int b=0;b<branches;++b) {
            float bx=x,slope=(b-(branches-1)*0.5f)*0.5f+(random()-0.5f)*0.3f,top=height*(0.8f+random()*0.25f);
            for(float z=split;z<top;z+=block) {
                bx+=slope*block*0.5f+(random()-0.5f)*0.9f;
                int shade=random()<0.5f?1:2;
                if(z<=front&&z<collapse)put(bx,0,z,z>top-block*2?std::max(1.0f,block-1):block,shade);
            }
        }
        int specks=thick?20:40;
        for(int i=0;i<specks;++i) {
            float f=std::pow(random(),0.9f),z=f*height*1.05f,side=gauss()*(base*0.35f*(1-f)+1.5f+f*height*0.28f),depth=gauss()*2;
            float out=(side<0?-1:1)*(0.1f+random()*0.15f),up=0.1f+random()*0.2f*f;
            float size=thick?(random()<0.5f?2:3):(random()<0.25f?2:1),die=6+random()*9+(1-f)*5;
            int shade=std::min(2,(int)(random()*3));
            if(z>front||t>die)continue;
            float k=std::max(0.0f,t-std::max(1.0f,z/height*4));
            put(side+out*k,depth,z+up*k,t>die-2?1:size,shade);
        }
    }
}
// Rings: up to three full circles, the inner ones a few tics behind, growing
// a frame every frameTics and thickening as they grow. They start at the brightest
// texel of the liquid's flat, lit like the spouts, step down toward
// the liquid's own color as they age, and are gone at the end.
void buildRings(Uniforms &camera) {
    if(!settings.splashes||splashLevelSerial!=r_levelserial) {rings.clear();spouts.clear();return;}
    float now=gameSeconds();
    rings.erase(std::remove_if(rings.begin(),rings.end(),[&](const Ring &r){return now-r.born>r.life||now<r.born-0.1f;}),rings.end());
    for(const Ring &ring:rings) {
        float age=std::max(0.0f,std::floor((now-ring.born)*TICRATE/frameTics)*frameTics/TICRATE),fade=(1-age/ring.life)*std::max(ring.strength,0.5f);
        const sector_t &sector=sectors[ring.sector];
        float z=floorZ(&sector)+decalLift(ring.x,ring.y,camera)+0.1f;
        Image &flat=lumpImage(firstflat+flattranslation[sector.floorpic],true);
        float light=liquidLight(ring.sector,flat,ring.x,ring.y,z,camera);
        const auto &brightest=brightestColor(flat);const auto &base=sectorMist[ring.sector].color;
        std::array<float,3> albedo;
        for(int c=0;c<3;++c)albedo[c]=brightest[c];
        // The flashlight lights the whole ring by its center.
        float beamAmount=0;std::array<float,3> beamColor={};
        if(flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0&&camera.effects[2]==0) {
            const Flash &beam=flashes.lights[0];beamAmount=flashAt(ring.x,ring.y,z+1,beam);
            for(int c=0;c<3;++c)beamColor[c]=beam.color[c];
        }
        // Points along each circle close enough to touch at the nearest
        // depth it can have on screen, so it reads as one unbroken line.
        float cx=ring.x-camera.eye[0],cy=ring.y-camera.eye[1],cz=z-camera.eye[2];
        float centerDepth=cx*camera.forward[0]+cy*camera.forward[1]+cz*camera.forward[2];
        bool thick=sectorMist[ring.sector].thick;
        for(int j=0;j<3;++j) {
            float inner=age-j*(thick?0.2f:0.12f),step=fade*(1-0.25f*j);
            if(inner<0||step<0.12f)break;
            float r=4+ring.speed*inner,width=(thick?2.0f:0.8f)+r*0.06f;
            float spacing=std::max(std::max(4.0f,centerDepth-r)/(100*camera.projection[1])*0.9f,width*0.7f);
            int points=std::clamp((int)(2*doomPi*r/std::max(spacing,0.05f)),12,512);
            // Palette steps from the bright crest toward the liquid's color.
            float mixed=std::round(step*4)/4;
            std::array<float,3> albedoNow,tint;
            for(int c=0;c<3;++c)albedoNow[c]=base[c]+(albedo[c]-base[c])*mixed;
            if(!thick)albedoNow=foam(albedoNow,0.3f,light); // Water's rings in its paler foam blue.
            for(int c=0;c<3;++c) {
                tint[c]=std::min(1.0f,albedoNow[c]*light+albedoNow[c]*beamAmount*beamColor[c]);
            }
            for(int k=0;k<points;++k) {
                float angle=k*2*doomPi/points;
                pixelBlock(particleVertices,camera,ring.x+std::cos(angle)*r,ring.y+std::sin(angle)*r,z,width,thick?0.6f:1.0f,tint,true);
            }
        }
    }
    buildSpouts(camera);
}

// A thick liquid's color for its bubbles and collars: the brightest of a few
// texels of its flat picked by hash, lit like the spouts at (x,y,z).
std::array<float,3> gooColor(int sector,uint32_t seed,float x,float y,float z,const Uniforms &camera) {
    Image &flat=lumpImage(firstflat+flattranslation[sectors[sector].floorpic],true);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int best=0,bestSum=-1;
    for(int k=0;k<4;++k) {
        int texel=std::min(4095,(int)(hashUnit(k,(int)seed,seed^0xb0bu)*4096));
        int index=flat.pixels[2*texel],sum=palette[index*3]+palette[index*3+1]+palette[index*3+2];
        if(sum>bestSum){bestSum=sum;best=index;}
    }
    float light=liquidLight(sector,flat,x,y,z,camera);
    return {std::min(1.0f,palette[best*3]/255.0f*light*1.15f),std::min(1.0f,palette[best*3+1]/255.0f*light*1.15f),std::min(1.0f,palette[best*3+2]/255.0f*light*1.15f)};
}
// Bubbles on thick liquids near the eye, stateless like the dust: about a
// third of the 32-unit cells over them bubble once every few seconds, at a
// new spot each time. A bubble domes up in three steps, hangs, then bursts
// into four specks and a small dotted ring. Tic-stepped.
void buildBubbles(const Uniforms &camera) {
    if(!settings.splashes||camera.effects[2]!=0||sectorMist.size()!=(size_t)numsectors)return;
    constexpr float cell=32,reach=640;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].thick||sectors[i].floorpic==skyflatnum)continue;
        float floor=floorZ(&sectors[i]);
        if(camera.eye[2]<floor+1)continue;
        for(int leaf:sectorFloors[i]) {
            const auto &b=floorBounds[leaf];const auto &poly=floors[leaf];
            if(poly.size()<3)continue;
            float dx=std::max({b[0]-camera.eye[0],0.0f,camera.eye[0]-b[2]}),dy=std::max({b[1]-camera.eye[1],0.0f,camera.eye[1]-b[3]});
            if(std::hypot(dx,dy)>reach)continue;
            for(int gy=(int)std::floor(b[1]/cell);gy<=(int)std::floor(b[3]/cell);++gy)
            for(int gx=(int)std::floor(b[0]/cell);gx<=(int)std::floor(b[2]/cell);++gx) {
                if(hashUnit(gx,gy,0xb0b1u)>0.35f)continue;
                float period=2.5f+4*hashUnit(gx,gy,0xb0b2u),clock=now+hashUnit(gx,gy,0xb0b3u)*period;
                int cycle=(int)std::floor(clock/period);float s=clock-cycle*period;
                if(s>=1.4f)continue;
                uint32_t seed=(uint32_t)cycle*2654435761u^(uint32_t)(gx*73856093)^(uint32_t)(gy*19349663);
                float x=(gx+0.15f+0.7f*hashUnit(gx,gy,seed))*cell,y=(gy+0.15f+0.7f*hashUnit(gx,gy,seed^1u))*cell;
                if(x<b[0]||x>b[2]||y<b[1]||y>b[3])continue;
                bool inside=true;
                for(size_t n=0;n<poly.size()&&inside;++n) {
                    Point a=poly[n],c=poly[(n+1)%poly.size()];
                    inside=(c.x-a.x)*(y-a.y)-(c.y-a.y)*(x-a.x)>=0;
                }
                if(!inside||std::hypot(x-camera.eye[0],y-camera.eye[1])>reach)continue;
                auto color=gooColor(i,seed,x,y,floor,camera);
                if(s<1.15f) {
                    float step=s<0.3f?1:s<0.6f?2:3;
                    pixelBlock(particleVertices,camera,x,y,floor+0.2f,1.6f*step,s<0.9f?0.6f:0.8f,color,true,&color);
                } else {
                    float q=(s-1.15f)/0.25f,r=2+8*q,z=floor+0.2f+5*std::sin(doomPi*q);
                    for(int k=0;k<4;++k)
                        pixelBlock(particleVertices,camera,x+(k&1?r:-r)*0.7f,y+(k&2?r:-r)*0.7f,z,1.2f,1,color,false,&color);
                    for(int k=0;k<8;++k) {
                        if(hashUnit(k,(int)seed,seed^7u)<q)continue;
                        float angle=(k+0.5f)*doomPi/4;
                        pixelBlock(particleVertices,camera,x+std::cos(angle)*r,y+std::sin(angle)*r,floor+0.2f,1.2f,1,color,true,&color);
                    }
                }
            }
        }
    }
}
void splashesShutdown() {splashWatch.clear();rings.clear();spouts.clear();}
} // namespace doom3d
