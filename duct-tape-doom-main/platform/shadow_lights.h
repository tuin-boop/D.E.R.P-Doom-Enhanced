#ifndef DOOM_SHADOW_LIGHTS_H
#define DOOM_SHADOW_LIGHTS_H
#include <array>
#include <algorithm>
#include <cmath>
namespace doom3d {
constexpr int maxShadowLights=8;
struct ShadowLight { float x=0,y=0,z=0,strength=0; };
// Keep nearby lamps even when their flicker changes their ranking.
// A baked and dynamic copy of the same lamp must not darken it twice.
struct ShadowLights {
    std::array<ShadowLight,maxShadowLights> lights{};
    void add(ShadowLight light) {
        if(!std::isfinite(light.strength)||light.strength<=0)return;
        for(auto &old:lights)if(old.strength>0&&std::abs(old.x-light.x)<.1f&&std::abs(old.y-light.y)<.1f&&std::abs(old.z-light.z)<.1f){old.strength=std::max(old.strength,light.strength);return;}
        auto weakest=std::min_element(lights.begin(),lights.end(),[](const auto&a,const auto&b){return a.strength<b.strength;});
        if(light.strength>weakest->strength)*weakest=light;
    }
};
struct ShadowSources {
    std::array<int,maxShadowLights> ids;
    ShadowSources(){ids.fill(-1);}
    std::array<float,maxShadowLights> amounts{};
    void add(int id,float amount) {
        if(id<0||amount<=0)return;
        for(int i=0;i<maxShadowLights;++i)if(ids[i]==id){amounts[i]=std::max(amounts[i],amount);return;}
        int slot=int(std::min_element(amounts.begin(),amounts.end())-amounts.begin());
        if(amount>amounts[slot]){ids[slot]=id;amounts[slot]=amount;}
    }
};
}
#endif
