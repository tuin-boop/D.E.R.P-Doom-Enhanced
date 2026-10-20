#include "scene.h"
#include <algorithm>
#include <fstream>
#include <cstring>
namespace lab {
bool Scene::fits(Vec feet,float radius,float height)const{
 return terrain.fits(feet,radius,height)&&(!hasDoor||door.fits(feet-Vec{0,0,doorZ},radius,height,false));
}
Vec Scene::move(Vec feet,Vec delta,float radius,float height)const{
 int steps=std::max(1,int(std::ceil(length(delta)/2)));Vec step=delta*(1.f/steps);
 for(int i=0;i<steps;++i){
  if(fits(feet+step,radius,height)){feet=feet+step;continue;}
  Vec old=feet;
  for(Vec s:{Vec{step.x,0,0},Vec{0,step.y,0},Vec{0,0,step.z}})if(fits(feet+s,radius,height))feet=feet+s;
  if(length(feet-old)<1e-6f)break;
 }return feet;
}
bool Scene::ray(Vec start,Vec direction,float range,Hit& hit,int& owner)const{
 bool found=terrain.ray(start,direction,range,hit);owner=0;
 Hit moving;
 if(hasDoor&&door.ray(start-Vec{0,0,doorZ},direction,found?hit.distance:range,moving)){
  hit=moving;hit.point.z+=doorZ;owner=1;found=true;
 }return found;
}
bool Scene::blast(Vec center,float radius,int owner){
 // A direct impact belongs to the nearest surface found by ray(). Applying
 // it to both fields duplicates damage on a leaf hidden inside its housing.
 if(owner==0){bool changed=terrain.blast(center,radius);if(changed)terrain.removeSmallFragments();return changed;}
 if(owner==1&&hasDoor){bool changed=door.blast(center-Vec{0,0,doorZ},radius);if(changed)door.removeSmallFragments();return changed;}
 return false;
}
void Scene::tick(float dt,Vec player,float radius,float height){
 if(!hasDoor||!std::isfinite(dt)||dt<=0)return;
 float distance=std::clamp(doorTarget-doorZ,-64*dt,64*dt);
 int steps=std::max(1,int(std::ceil(std::abs(distance))));
 for(int i=0;i<steps;++i){
  float next=doorZ+distance/steps;
  if(!door.fits(player-Vec{0,0,next},radius,height,false)){
   // Reverse a closing door, stop an opening one. No teleporting the player.
   if(distance<0)doorTarget=doorTravel;
   break;
  }
  doorZ=next;
 }
}
void Scene::createDoorTest(){
 hasDoor=true;doorZ=doorTarget=0;doorTravel=88;
 terrain.create(65,49,57,4,{0,0,-8});
 for(int z=0;z<terrain.nz;++z)for(int y=0;y<terrain.ny;++y)for(int x=0;x<terrain.nx;++x){
  Vec p=terrain.point(x,y,z);
  float room=std::max({8-p.x,p.x-248,8-p.y,p.y-184,-p.z,p.z-208});
  // Solid dividing wall with an 80-high, 112-wide doorway.
  float wall=std::min(p.x-112,144-p.x);
  float opening=std::max({40-p.y,p.y-152,-p.z,p.z-80});
  float value=std::max(room,std::min(wall,opening));
  if(std::abs(value)<.001f)value=-.001f;
  terrain.base[terrain.index(x,y,z)]=terrain.density[terrain.index(x,y,z)]=value;
 }
 terrain.spawn={56,96,.1f};terrain.yaw=0;terrain.rebuild();
 door.create(13,33,25,4,{104,32,-8});
 for(int z=0;z<door.nz;++z)for(int y=0;y<door.ny;++y)for(int x=0;x<door.nx;++x){
  Vec p=door.point(x,y,z);
  // Recess both leaf faces so a raised door cannot share the housing's plane.
  float value=std::min({p.x-116,140-p.x,p.y-40,152-p.y,p.z,80-p.z});
  if(std::abs(value)<.001f)value=-.001f;
  door.base[door.index(x,y,z)]=door.density[door.index(x,y,z)]=value;
 }door.rebuild();
}
bool Scene::save(const std::string& path)const{
 std::ofstream f(path,std::ios::binary);return save(f);
}
bool Scene::save(std::ostream& f)const{
 f.write("SVS1",4);
 unsigned active=hasDoor?1:0;f.write((char*)&active,4);
 f.write((char*)&doorZ,4);f.write((char*)&doorTarget,4);f.write((char*)&doorTravel,4);
 return terrain.save(f)&&(!hasDoor||door.save(f))&&bool(f);
}
bool Scene::load(const std::string& path){
 std::ifstream f(path,std::ios::binary);return load(f);
}
bool Scene::load(std::istream& f){
 char magic[4];f.read(magic,4);if(!f)return false;
 Scene next;
 if(!std::memcmp(magic,"SVL1",4)){f.seekg(0);if(!next.terrain.load(f))return false;}
 else if(!std::memcmp(magic,"SVS1",4)){
  unsigned active;f.read((char*)&active,4);f.read((char*)&next.doorZ,4);f.read((char*)&next.doorTarget,4);f.read((char*)&next.doorTravel,4);
  if(!f||active>1||!std::isfinite(next.doorZ)||!std::isfinite(next.doorTarget)||!std::isfinite(next.doorTravel)||next.doorTravel<=0||next.doorTravel>4096||next.doorZ<0||next.doorZ>next.doorTravel||next.doorTarget<0||next.doorTarget>next.doorTravel)return false;
  next.hasDoor=active!=0;
  if(!next.terrain.load(f)||(next.hasDoor&&!next.door.load(f)))return false;
 }else return false;
 *this=std::move(next);return true;
}
}
