#include "../platform/weapon_transform.h"
#include "../platform/weapon_switch.h"
#include <cstdio>
#include <limits>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool near(float a,float b){return std::abs(a-b)<0.0001f;}
int main() {
    using namespace doomweapon;
    Motion m;Transform t;t.x=20;t.y=32;
    CHECK(m.set(t));CHECK(near(m.sample(0).x,20)); // First frame cannot fly in from origin.
    m.beforeTick();t.x=30;t.y=42;CHECK(m.set(t));
    CHECK(near(m.sample(0).x,20));CHECK(near(m.sample(.5f).x,25));CHECK(near(m.sample(1).x,30));
    CHECK(near(m.sample(-1).x,20));CHECK(near(m.sample(2).x,30));
    Transform a,b;a.rotation=350;b.rotation=10;CHECK(near(interpolate(a,b,.5f).rotation,360));
    b.rotation=90;b.pivotX=10;b.pivotY=10;b.scaleX=2;b.scaleY=3;
    auto p=point(b,11,10);CHECK(near(p[0],10)&&near(p[1],7.5f));
    b.scaleX=-2;p=point(b,10,11);CHECK(near(p[0],7.6f)&&near(p[1],10));
    t.x=80;CHECK(m.set(t,false));CHECK(near(m.sample(0).x,80)); // Explicit no-interpolation transition.
    t.x=std::numeric_limits<float>::infinity();CHECK(!m.set(t));CHECK(near(m.sample(.5f).x,80));
    m.reset();t.x=-10;CHECK(m.set(t));CHECK(near(m.sample(0).x,-10)); // Respawn/load isolation.
    // Reference timelines contain real alternate art and independent hands.
    bool shotgunHand=false,ssgHand=false,rocketFrame=false;
    for(int weapon=0;weapon<9;++weapon)for(int variant=0;variant<4;++variant){
        const auto&c=switchClips[weapon*8+variant*2];
        for(unsigned tick=0;tick<c.count;++tick){
            const auto&f=switchFrames[c.first+tick];
            if(weapon==3&&tick==10)CHECK(f.count>12);
            for(unsigned j=0;j<f.count;++j){
                const auto&p=switchPoses[f.first+j];CHECK(valid(p.transform));
                CHECK(p.transform.scaleX>0&&p.transform.scaleY>0);
                if(std::string(p.sprite)=="MRD1G0"){if(weapon==2)shotgunHand=true;if(weapon==8)ssgHand=true;}
                if(weapon==4&&std::string(p.sprite)=="MI1GC0")rocketFrame=true;
            }
        }
    }
    CHECK(shotgunHand&&ssgHand&&rocketFrame);
    WeaponSwitch sw;sw.tick(1,1);CHECK(sw.active());CHECK(sw.layers(1,true).empty());
    for(int i=0;i<16;++i)sw.tick(1,1);
    sw.tick(1,0);CHECK(sw.active()); // Settling survives the ready transition.
    sw.tick(1,0,true);CHECK(!sw.active()); // A shot wins immediately.
    sw.tick(1,2);CHECK(sw.active());
    sw.tick(2,1);CHECK(sw.active()); // Holster cannot leak into the next weapon.
    sw.reset();CHECK(!sw.active());
    // Shotgun raising clears the source interpolation flag on moving poses.
    // Native playback must still produce distinct in-between display frames.
    sw.tick(2,1);for(int i=0;i<7;++i)sw.tick(2,1);
    auto start=sw.layers(0,true),half=sw.layers(.5f,true),end=sw.layers(1,true);
    CHECK(!start.empty()&&start.size()==half.size()&&half.size()==end.size());
    bool moved=false;
    for(size_t i=0;i<start.size();++i){
        if(!near(start[i].transform.y,end[i].transform.y)){
            moved=true;CHECK(near(half[i].transform.y,(start[i].transform.y+end[i].transform.y)*.5f));
        }
    }
    CHECK(moved);
    puts("Weapon transforms: tick interpolation, pivot, scale, rotation, reset and finite-input checks passed");
}
