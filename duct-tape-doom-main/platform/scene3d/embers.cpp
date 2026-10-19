/* Embers: a few sparks rising from every flame, carried off by the game's
   own wind (Game_SectorWind) where it blows. */
#include "internal.h"

namespace doom3d {
namespace {
// The wind a flame's embers ride: its own sector's, or the nearest wind
// sector's within reach, weaker with distance, as if the draught pulled
// at the air around it. Map units per second.
struct FlameWind { float x=0,y=0,east=0,north=0; };
constexpr float windReach=256;
FlameWind windAt(const mobj_t &thing) {
    FlameWind wind{units(thing.x),units(thing.y)};
    return wind;
}
std::unordered_map<const mobj_t*,FlameWind> flameWinds;
int lastLevelTime=0;unsigned flameLevel=~0u;
}
// Three embers per flame, each on its own 1.6-2.4 second cycle: it leaves
// the flame's tip, rises about 25 units a second and slows, swaying a
// little, and cools from the flame's own colour to a dark red until it
// goes out. Wind pushes it harder as it climbs out of the flame. One Doom
// pixel, snapped to the palette; it dies where it leaves open air.
void buildEmbers(const Uniforms &camera) {
    if(!settings.embers||camera.effects[2]!=0||seamCells.empty())return;
    if(leveltime<lastLevelTime||flameLevel!=r_levelserial){flameWinds.clear();flameLevel=r_levelserial;}
    lastLevelTime=leveltime;
    float now=gameSeconds();
    int emitted=0;
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!decorationLight(*thing))continue;
        BakeLight flame=decorationSource(*thing);
        if(std::hypot(flame.x-camera.eye[0],flame.y-camera.eye[1])>1200)continue;
        // Fire only: warm red-orange glows, as for the heat haze.
        if(!(flame.color[0]>flame.color[1]*1.15f&&flame.color[0]>flame.color[2]*1.5f))continue;
        auto found=flameWinds.find(thing);
        if(found==flameWinds.end()||found->second.x!=units(thing->x)||found->second.y!=units(thing->y))
            found=flameWinds.insert_or_assign(thing,windAt(*thing)).first;
        const FlameWind &wind=found->second;
        uint32_t seed=hash32((uint32_t)(uintptr_t)thing);
        for(int n=0;n<3;++n) {
            float life=1.6f+0.8f*hashUnit(n,0,seed),clock=now+life*hashUnit(n,1,seed);
            int cycle=(int)std::floor(clock/life);
            float age=clock-cycle*life,fade=age/life;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float rise=20+10*hashUnit(n,2,own),phase=hashUnit(n,3,own)*2*doomPi;
            float push=std::max(0.0f,age-0.15f); // Out of the flame before the wind takes it.
            float x=flame.x+(hashUnit(n,4,own)-0.5f)*6+3*std::sin(age*2.3f+phase)+wind.east*push*(0.8f+0.4f*hashUnit(n,5,own));
            float y=flame.y+(hashUnit(n,6,own)-0.5f)*6+3*std::cos(age*1.9f+phase)+wind.north*push*(0.8f+0.4f*hashUnit(n,5,own));
            float z=flame.z+rise*age-4*age*age;
            int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
            if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)continue;
            int at=seamCells[(size_t)cy*mapWidth+cx].own;
            if(at==noSector||z<floorZ(&sectors[at])+1||z>ceilZ(&sectors[at])-1)continue;
            std::array<float,3> color;
            const float cold[3]={0.45f,0.08f,0.02f};
            for(int c=0;c<3;++c) {
                float hot=std::min(1.0f,flame.color[c]*1.2f+0.15f);
                color[c]=hot+(cold[c]-hot)*fade*fade;
            }
            if(emitted++>=384)return;
            pixelBlock(particleVertices,camera,x,y,z,0.6f,1,color);
        }
    }
}
} // namespace doom3d
