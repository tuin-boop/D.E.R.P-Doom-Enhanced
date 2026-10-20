#pragma once
#include "volume.h"
namespace lab {
// A moving solid keeps its density samples in local coordinates. Translation
// never remeshes it or resamples damage, avoiding drift after door cycles.
class Scene {
public:
 Volume terrain,door;
 bool hasDoor=false;
 float doorZ=0,doorTarget=0,doorTravel=88;
 bool fits(Vec feet,float radius=12,float height=56)const;
 Vec move(Vec feet,Vec delta,float radius=12,float height=56)const;
 bool ray(Vec start,Vec direction,float range,Hit& hit,int& owner)const;
 bool blast(Vec center,float radius,int owner);
 void tick(float dt,Vec player,float radius=12,float height=56);
 void toggleDoor(){doorTarget=doorTarget>0?0:doorTravel;}
 void createDoorTest();
 bool save(const std::string& path)const;
 bool load(const std::string& path);
 bool save(std::ostream& stream)const;
 bool load(std::istream& stream);
};
}
