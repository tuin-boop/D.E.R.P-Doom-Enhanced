// Detailed, world-space impact particles. Physics runs once per game tic;
// drawing interpolates it without consuming the gameplay random stream.
struct ImpactParticle {
    float x,y,z,ox,oy,oz,vx,vy,vz,age,life,size,spin;
    int sector,kind; // 0 streak, 1 fragment, 2 dust, 3 flame
    std::array<float,3> color;
};
static std::vector<ImpactParticle> impactParticles;

struct GunPuff {float x,y,z;int tic;};
static std::vector<GunPuff> gunPuffs;
static unsigned impactSerial;
static void resetImpactLevel() {
    if(impactSerial!=r_levelserial){impactParticles.clear();gunPuffs.clear();impactSerial=r_levelserial;}
}
bool customGunPuff(const mobj_t *puff) {
    if(!settings.gunImpacts||puff->type!=MT_PUFF)return false;
    resetImpactLevel();
    for(const auto &p:gunPuffs){float dx=units(puff->x)-p.x,dy=units(puff->y)-p.y,dz=units(puff->z)-p.z;
        if(leveltime-p.tic<=TICRATE&&dx*dx+dy*dy<256&&std::abs(dz)<40)return true;}
    return false;
}
static void markGunImpact(float x,float y,float z){
    resetImpactLevel();if(gunPuffs.size()>=128)gunPuffs.erase(gunPuffs.begin());
    gunPuffs.push_back({x,y,z,leveltime});
}
static void impactParticle(float x,float y,float z,float vx,float vy,float vz,float life,float size,int kind,std::array<float,3> color) {
    resetImpactLevel();
    if(impactParticles.size()>=512)impactParticles.erase(impactParticles.begin());
    int sector=int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
    impactParticles.push_back({x,y,z,x,y,z,vx,vy,vz,0,life,size,randomRange(0,6.28f),sector,kind,color});
}
// Cross open portals, but reflect at solid wall sections. Scan only the
// current sector's boundary, never every line of a large map per particle.
static void moveImpactParticle(ImpactParticle &p,float dt) {
    float tx=p.x+p.vx*dt,ty=p.y+p.vy*dt,tz=p.z+p.vz*dt;
    float start=0;
    for(int crossing=0;crossing<12;++crossing) {
        sector_t &sec=sectors[p.sector];float best=2;line_t *edge=nullptr;
        float dx=tx-p.x,dy=ty-p.y;
        for(int n=0;n<sec.linecount;++n) {
            line_t *l=sec.lines[n];float ax=units(l->v1->x),ay=units(l->v1->y);
            float ex=units(l->dx),ey=units(l->dy),det=dx*ey-dy*ex;
            if(std::abs(det)<1e-6f)continue;
            float ox=ax-p.x,oy=ay-p.y,t=(ox*ey-oy*ex)/det,u=(ox*dy-oy*dx)/det;
            if(t>start+1e-5f&&t<=1&&u>=0&&u<=1&&t<best){best=t;edge=l;}
        }
        if(!edge){p.x=tx;p.y=ty;p.z=tz;break;}
        sector_t *next=edge->frontsector==&sec?edge->backsector:edge->frontsector;
        float at=p.z+(tz-p.z)*best;
        if(next&&at>std::max(units(sec.floorheight),units(next->floorheight))+.3f&&
           at<std::min(units(sec.ceilingheight),units(next->ceilingheight))-.3f) {
            p.sector=int(next-sectors);start=best;continue;
        }
        float len=std::hypot(units(edge->dx),units(edge->dy));
        float nx=units(edge->dy)/len,ny=-units(edge->dx)/len;
        float dot=p.vx*nx+p.vy*ny;if(dot>0){nx=-nx;ny=-ny;dot=-dot;}
        p.x+=dx*best+nx*.35f;p.y+=dy*best+ny*.35f;p.z=at;
        float bounce=p.kind<2?.45f:.1f;
        p.vx=(p.vx-(1+bounce)*dot*nx)*.8f;p.vy=(p.vy-(1+bounce)*dot*ny)*.8f;
        break;
    }
    const sector_t &sec=sectors[p.sector];float low=units(sec.floorheight)+.3f,high=units(sec.ceilingheight)-.3f;
    if(high<low){p.age=p.life;return;}
    if(p.z<low){p.z=low;p.vz=std::abs(p.vz)*.25f;p.vx*=.7f;p.vy*=.7f;}
    if(p.z>high){p.z=high;p.vz=-std::abs(p.vz)*.25f;}
}
void tickImpactParticles() {
    resetImpactLevel();
    constexpr float dt=1.f/TICRATE;
    for(auto &p:impactParticles) {
        p.ox=p.x;p.oy=p.y;p.oz=p.z;p.age+=dt;
        p.vz+=(p.kind==0?-245.f:0.f)*dt;
        float drag=1.f;p.vx*=drag;p.vy*=drag;
        moveImpactParticle(p,dt);
    }
    impactParticles.erase(std::remove_if(impactParticles.begin(),impactParticles.end(),[](const auto &p){return p.age>=p.life;}),impactParticles.end());

}

#include "uzi_impact_assets.inc"
// The source Uzi emits one animated voxel dust cloud, one flash/puff,
// and four animated sparks. All positions/timing remain render-only.
static void uziImpact(float x,float y,float z,float ax,float ay,float az) {
    markGunImpact(x,y,z);
    impactParticle(x,y,z,0,0,35,16.f/TICRATE,1,3,{1,1,1});
    impactParticle(x,y,z,ax*21,ay*21,15.75f,17.f/TICRATE,.65f,2,{1,1,1});
    for(int i=0;i<4;++i){float a=randomRange(0,2*doomPi),v=randomRange(1,i==0?3.f:2.f)*35;
        bool shortLife=randomRange(0,1)<.5f;
        impactParticle(x,y,z,std::cos(a)*v+ax*35,std::sin(a)*v+ay*35,randomRange(-1,3)*35,
                       (shortLife?3.f:6.f)*2/TICRATE,i==0?.6f:.4f,0,{shortLife?1.f:0.f,1,1});
    }
}
static void drawImpactParticles(const Uniforms &camera) {
    resetImpactLevel();
    auto *pal=(const byte*)W_CacheLumpName("PLAYPAL",PU_CACHE);
    std::vector<const ImpactParticle*> ordered;
    for(const auto &p:impactParticles)ordered.push_back(&p);
    auto depth=[&](const auto *p){return (p->x-camera.eye[0])*camera.forward[0]+(p->y-camera.eye[1])*camera.forward[1]+(p->z-camera.eye[2])*camera.forward[2];};
    std::sort(ordered.begin(),ordered.end(),[&](const auto *a,const auto *b){return depth(a)>depth(b);});
    for(const auto *ptr:ordered){const auto &p=*ptr;
        float t=std::max(0.f,p.age-(1-renderFraction)/TICRATE);
        float x=p.ox+(p.x-p.ox)*renderFraction,y=p.oy+(p.y-p.oy)*renderFraction,z=p.oz+(p.z-p.oz)*renderFraction;
        if(depth(ptr)<2)continue;
        if(p.kind==2){
            int tic=int(t*TICRATE),frame=tic<2?0:tic<5?1:tic<9?2:tic<13?3:4;
            float cs=std::cos(p.spin),sn=std::sin(p.spin),light=std::max(.25f,lighting(&sectors[p.sector]));
            for(int i=0;i<uziDustCounts[frame];++i){const auto &f=uziDustFrames[frame][i];
                auto v=[&](float a,float b,float c){a+=f.x;b+=f.y;c+=f.z;Vertex out={x+(cs*a-sn*b)*p.size,y+(sn*a+cs*b)*p.size,z+c*p.size,0,0,1,0};
                    float shade=light*(f.axis<2?.8f:f.axis<4?.65f:1.f);out.red=f.r/255.f*shade;out.green=f.g/255.f*shade;out.blue=f.b/255.f*shade;return out;};
                if(f.axis<2){float a=f.axis==0?1.f:0.f;quad(particleVertices,v(a,0,0),v(a,1,0),v(a,1,-1),v(a,0,-1));}
                else if(f.axis<4){float b=f.axis==2?1.f:0.f;quad(particleVertices,v(0,b,0),v(1,b,0),v(1,b,-1),v(0,b,-1));}
                else {float c=f.axis==4?-1.f:0.f;quad(particleVertices,v(0,0,c),v(1,0,c),v(1,1,c),v(0,1,c));}
            }
        }else{
            int frame=p.kind==3?std::min(15,int(t*TICRATE)):std::min(4,(p.color[0]>0?2+int(t*TICRATE/2):std::max(0,int(t*TICRATE/2)-1)));
            const auto *rects=p.kind==3?uziFFrames[frame]:uziSFrames[frame];
            int count=p.kind==3?uziFCounts[frame]:uziSCounts[frame];
            for(int i=0;i<count;++i){const auto &r=rects[i];
                auto v=[&](float u,float h){Vertex out={x+camera.right[0]*u*p.size+camera.up[0]*h*p.size,y+camera.right[1]*u*p.size+camera.up[1]*h*p.size,z+camera.right[2]*u*p.size+camera.up[2]*h*p.size,0,0,(p.kind==3?.7f:1.f)*r.a/255.f,0};
                    out.red=(p.kind==3?pal[r.r*3]:r.r)/255.f;out.green=(p.kind==3?pal[r.r*3+1]:r.g)/255.f;out.blue=(p.kind==3?pal[r.r*3+2]:r.b)/255.f;return out;};
                quad(particleVertices,v(r.x,r.y),v(r.x+r.w,r.y),v(r.x+r.w,r.y-1),v(r.x,r.y-1));
            }
        }
    }
}
