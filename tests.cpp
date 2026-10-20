#include "scene.h"
#include <iostream>
#include <map>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <chrono>
using namespace lab;
static void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static void closed(const Volume&v){
 using Key=std::array<float,3>;using Edge=std::pair<Key,Key>;std::map<Edge,int> counts;
 auto key=[](Vec p){return Key{p.x,p.y,p.z};};
 for(const auto &chunk:v.chunks)for(const auto&t:chunk.triangles){
  Key pts[3]={key(t.a),key(t.b),key(t.c)};
  for(int i=0;i<3;++i){Key a=pts[i],b=pts[(i+1)%3];if(a==b)continue;if(b<a)std::swap(a,b);++counts[{a,b}];}
 }
 int bad=0;for(const auto&e:counts)if(e.second!=2)++bad;
 if(bad)throw std::runtime_error("Non-manifold/open mesh edges: "+std::to_string(bad));
}
int main(int argc,char **argv){try{
 Volume fragments;fragments.create(12,12,12,4,{});
 std::fill(fragments.density.begin(),fragments.density.end(),-4.f);
 fragments.density[fragments.index(8,8,8)]=4;
 // A diagonal chain is connected in the tetrahedral mesh and must survive.
 for(int i=2;i<7;++i)fragments.density[fragments.index(i,i,2)]=4;
 fragments.base=fragments.density;fragments.rebuild();closed(fragments);
 require(fragments.removeSmallFragments()==1,"Tiny fragment cleanup removed valid diagonal structure");
 require(fragments.density[fragments.index(8,8,8)]<0,"Floating fragment survived cleanup");closed(fragments);
 require(fragments.removeSmallFragments()==0,"Fragment cleanup was not stable");
 std::cout<<"PASS: tiny isolated solid removed; diagonal connections and closed mesh preserved.\n";
 Scene moving;moving.createDoorTest();closed(moving.terrain);closed(moving.door);
 Vec start{56,96,.1f};Hit doorHit;int owner=-1;
 require(moving.ray(start+Vec{0,0,41},{1,0,0},200,doorHit,owner)&&owner==1,"Closed moving door must receive shots");
 require(moving.move(start,{144,0,0}).x<105,"Closed moving door must block walking");
 moving.door.blast({112,96,42},18);closed(moving.door);
 Hit localHit;require(moving.door.ray({56,96,42},{1,0,0},160,localHit),"Damage test ray missed door");
 auto damage=moving.door.density;unsigned meshRevision=moving.door.revision;
 moving.toggleDoor();moving.tick(.25f,start);
 require(moving.ray({56,96,42+moving.doorZ},{1,0,0},160,doorHit,owner)&&owner==1&&std::abs(doorHit.distance-localHit.distance)<.001f,"Damaged shot surface did not follow door");
 require(moving.save("moving-roundtrip.svs"),"Mid-motion save failed");
 Scene midway;require(midway.load("moving-roundtrip.svs"),"Mid-motion reload failed");
 require(midway.doorZ==moving.doorZ&&midway.doorTarget==moving.doorTarget,"Mid-motion state changed");
 for(int i=0;i<180;++i){moving.tick(1.f/60,start);midway.tick(1.f/60,start);}
 require(midway.doorZ==moving.doorZ,"Reloaded door did not resume motion");
 require(moving.doorZ==moving.doorTravel,"Door failed to open");
 require(moving.move(start,{144,0,0}).x>199,"Raised door still blocks passage");
 require(moving.door.density==damage&&moving.door.revision==meshRevision,"Door motion altered/remeshed local damage");
 moving.toggleDoor();Vec under{128,96,.1f};
 for(int i=0;i<180;++i){moving.tick(1.f/60,under);require(moving.fits(under),"Closing door embedded player");}
 require(moving.doorTarget==moving.doorTravel,"Blocked closing door did not reopen");
 require(moving.save("moving-roundtrip.svs"),"Moving scene save failed");
 Scene resumed;require(resumed.load("moving-roundtrip.svs"),"Moving scene load failed");
 require(resumed.door.density==damage&&resumed.doorZ==moving.doorZ&&resumed.doorTarget==moving.doorTarget,"Door save/load lost transform or damage");
 resumed.toggleDoor();for(int i=0;i<180;++i)resumed.tick(1.f/60,start);
 require(resumed.doorZ==0,"Door did not close after player left");
 for(int i=0;i<20;++i){resumed.toggleDoor();for(int n=0;n<90;++n)resumed.tick(1.f/60,start);}
 require(resumed.door.density==damage,"Repeated motion drifted damage");
 // A tunnel through the moving leaf must pass shots and bodies in both directions.
 for(float x:{112.f,128.f,144.f})resumed.door.blast({x,96,40},48);
 closed(resumed.door);
 require(resumed.move(start,{144,0,0}).x>199,"Blasted moving leaf blocks passage");
 require(resumed.move({200,96,.1f},{-144,0,0}).x<57,"Reverse blasted door passage blocked");
 require(resumed.ray(start+Vec{0,0,41},{1,0,0},200,doorHit,owner)&&owner==0,"Shots still hit removed door material");
 std::cout<<"PASS: moving door shots, walking, persistent local damage, obstruction reversal, 20 cycles, save/load and blasted passage.\n";
 Scene overlap;overlap.createDoorTest();overlap.doorZ=48;
 auto originalDoor=overlap.door.density;
 require(overlap.ray({56,96,100},{1,0,0},200,doorHit,owner)&&owner==0,"Housing must hide raised leaf");
 require(overlap.blast(doorHit.point-doorHit.normal*7,18,owner),"Housing impact changed nothing");
 require(overlap.door.density==originalDoor,"Housing impact copied into hidden door");
 auto damagedHousing=overlap.terrain.density;
 require(overlap.ray({56,96,65},{1,0,0},200,doorHit,owner)&&owner==1,"Exposed raised leaf must receive hit");
 require(overlap.blast(doorHit.point-doorHit.normal*7,18,owner),"Leaf impact changed nothing");
 require(overlap.terrain.density==damagedHousing,"Leaf impact copied into housing");
 overlap.doorTarget=0;for(int i=0;i<90;++i)overlap.tick(1.f/60,start);
 require(overlap.terrain.density==damagedHousing,"Moving damaged leaf altered housing");
 closed(overlap.terrain);closed(overlap.door);
 std::cout<<"PASS: upper housing and moving leaf impacts are not duplicated across owners.\n";
 Volume v;v.create(33,33,33,4,{});
 for(int z=0;z<v.nz;++z)for(int y=0;y<v.ny;++y)for(int x=0;x<v.nx;++x){
  Vec p=v.point(x,y,z);
  float room=std::max({8-p.x,p.x-120,8-p.y,p.y-120,8-p.z,p.z-112});
  float wall=std::min(p.x-56,72-p.x);
  float value=std::max(room,wall);if(std::abs(value)<.001f)value=.001f;
  v.base[v.index(x,y,z)]=v.density[v.index(x,y,z)]=value;
 }
 v.rebuild();closed(v);
 Hit h;require(v.ray({32,64,48},{1,0,0},100,h)&&h.distance<25,"Original wall ray missed");
 Vec before=v.move({32,64,12},{64,0,0});require(before.x<45,"Original wall did not stop player");
 require(v.blast({64,64,48},40),"Blast did not modify volume");closed(v);
 require(v.ray({32,64,48},{1,0,0},100,h)&&h.distance>80,"Ray did not pass opened wall");
 Vec after=v.move({32,64,12},{64,0,0});require(after.x>95,"Opened wall still blocks player");
 require(v.move(after,{-64,0,0}).x<33,"Reverse passage blocked");
 require(v.fits(after),"Player ended inside material");
 v.spawn=after;require(v.save("roundtrip.svl"),"Save failed");Volume copy;require(copy.load("roundtrip.svl"),"Reload failed");
 require(v.density==copy.density&&v.base==copy.base,"Density changed during save/load");closed(copy);
 require(copy.move({32,64,12},{64,0,0}).x>95,"Save/load changed collision");
 require(copy.move(after,{2000,0,0}).x<120,"Movement escaped outer boundary");
 size_t all=copy.chunks.size();std::vector<unsigned> chunkVersions;
 for(const auto& c:copy.chunks)chunkVersions.push_back(c.revision);
 require(copy.blast({8,18,18},8),"Local wall blast changed nothing");require(copy.lastRebuilt<int(all),"Blast rebuilt every chunk");closed(copy);
 int changedChunks=0;
 for(size_t n=0;n<all;++n)if(copy.chunks[n].revision!=chunkVersions[n]){
  require(copy.chunks[n].revision==chunkVersions[n]+1,"Chunk revision advanced incorrectly");++changedChunks;
 }
 require(changedChunks==copy.lastRebuilt,"Render cache invalidation does not match rebuilt chunks");
 // Cut across floor/wall/ceiling junctions; shared topology must remain closed.
 copy.blast({64,64,10},24);copy.blast({64,64,110},24);closed(copy);
 for(int i=0;i<24;++i)copy.blast({48.f+i*2,64,48},18);closed(copy);
 std::cout<<"PASS: closed meshes, chunk seams, floor/ceiling joins, ray hits, two-way walking, bounded movement, local rebuild and save/load.\n";
 for(int i=1;i<argc;++i){
  Volume f;require(f.load(argv[i]),"Fixture load failed");closed(f);
  std::cout<<"PASS fixture "<<argv[i]<<": "<<f.triangleCount()<<" triangles, closed mesh.\n";
  for(float x:{1344.f,1360.f,1376.f})f.blast({x,-3456,32},36);
  closed(f);
  Vec entrance{1320,-3456,.5f};require(f.fits(entrance),"Fixture entrance not clear");
  Vec crossed=f.move(entrance,{80,0,0});require(crossed.x>1399,"Fixture door blocks walking after excavation");
  require(f.move(crossed,{-80,0,0}).x<1321,"Fixture return passage blocked");
  require(!f.ray({1320,-3456,32},{1,0,0},100,h)||h.distance>80,"Fixture shot still hits removed doorway");
  f.blast({1352,-3456,0},24);f.blast({1352,-3456,72},24);closed(f);
  require(f.save("fixture-roundtrip.svl"),"Fixture save failed");Volume restored;require(restored.load("fixture-roundtrip.svl"),"Fixture reload failed");
  require(restored.density==f.density,"Fixture cut changed after reload");closed(restored);
  require(restored.move(entrance,{80,0,0}).x>1399,"Reloaded fixture blocks passage");
  Vec landed=restored.move(crossed,{0,0,-300});require(landed.z>restored.origin.z&&restored.fits(landed),"Fall escaped fixture or embedded the player");
  auto started=std::chrono::steady_clock::now();
  for(int n=0;n<100;++n){restored.fits(entrance);restored.ray(entrance+Vec{0,0,41},{1,0,0},256,h);}
  std::cout<<"Mean capsule + ray query: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()/100<<" ms.\n";
  std::cout<<"PASS fixture cuts: doorway traversal both ways, shot passage, floor/ceiling joins, save/load.\n";
 }
 return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
