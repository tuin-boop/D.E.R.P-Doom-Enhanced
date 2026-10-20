#pragma once
#include "volume.h"

namespace lab {
struct MapEdge { Vec a,b; int line=-1; };
struct MapRegion {
 int sector=-1;
 float floor=0,ceiling=0;
 bool breakFloor=true,breakCeiling=true;
 bool movingFloor=false,movingCeiling=false;
 std::vector<MapEdge> edges;
};
// Immutable XY topology plus current sector heights. Tiles use absolute grid
// coordinates: neighbours sample exactly the same positions at shared edges.
// No full-map 3D allocation, and no artificial skin around individual tiles.
class MapSource {
public:
 static constexpr int tileCells=16;
 static constexpr float step=4;
 void reset(std::vector<MapRegion> regions);
 bool heights(int sector,float floor,float ceiling);
 float sample(Vec point)const;
 float depth(Vec point)const;
 Volume tile(int x,int y,int z,bool mesh=true)const;
 size_t regionCount()const{return regions.size();}
 std::vector<MapRegion> topology()const;
 bool solidOwner(Vec point,int& sector,int& plane,float& anchor)const;
 bool damageAllowed(Vec point,int sector=-1,int plane=0)const;
 bool bounds(int sector,Vec& lo,Vec& hi)const;
private:
 struct Region {
  MapRegion map;
  Vec lo,hi;
 };
 std::vector<Region> regions;
 static float horizontal(const Region&,float x,float y);
};
}
