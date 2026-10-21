#include "map_source.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace lab {
std::vector<MapRegion> MapSource::topology()const{
 std::vector<MapRegion> result;for(const auto& r:regions)result.push_back(r.map);return result;
}
bool MapSource::bounds(int sector,Vec& lo,Vec& hi)const{
 for(const auto& r:regions)if(r.map.sector==sector){lo=r.lo;hi=r.hi;return true;}
 return false;
}
bool MapSource::solidOwner(Vec p,int& sector,int& plane,float& anchor)const{
 for(const auto& r:regions)if(horizontal(r,p.x,p.y)<=.001f){
  if(p.z>=r.map.ceiling){sector=r.map.sector;plane=1;anchor=r.map.ceiling;return true;}
  if(p.z<=r.map.floor){sector=r.map.sector;plane=0;anchor=r.map.floor;return true;}
 }
 return false;
}
bool MapSource::damageAllowed(Vec p,int sector,int plane)const{
 bool owned=sector<0;
 for(const auto& r:regions){
  if(r.map.sector!=sector&&r.map.breakFloor&&r.map.breakCeiling&&
     (sector>=0||(!r.map.movingFloor&&!r.map.movingCeiling)))continue;
  if(p.x<r.lo.x||p.x>r.hi.x||p.y<r.lo.y||p.y>r.hi.y)continue;
  if(horizontal(r,p.x,p.y)>.001f)continue;
  if(sector<0&&((r.map.movingFloor&&p.z<=r.map.floor)||(r.map.movingCeiling&&p.z>=r.map.ceiling)))return false;
  if(r.map.sector==sector){
   owned=plane?p.z>=r.map.ceiling-.02f:p.z<=r.map.floor+.02f;
  }
  // Protect the liquid/sky surface, not the entire solid column beneath or
  // above it: a wall beside a lava pool must remain destructible.
  if(p.z<=r.map.floor&&p.z>=r.map.floor-step&&!r.map.breakFloor)return false;
  if(p.z>=r.map.ceiling&&p.z<=r.map.ceiling+step&&!r.map.breakCeiling)return false;
 }
 return owned;
}
void MapSource::reset(std::vector<MapRegion> input){
 std::vector<Region> next;
 for(auto& map:input){
  if(!std::isfinite(map.floor)||!std::isfinite(map.ceiling))
   throw std::invalid_argument("Non-finite sector height");
  if(map.edges.empty())continue;
  Region r; r.lo={INFINITY,INFINITY,0};r.hi={-INFINITY,-INFINITY,0};
  for(const auto& edge:map.edges)for(Vec p:{edge.a,edge.b}){
   if(!std::isfinite(p.x)||!std::isfinite(p.y))
    throw std::invalid_argument("Non-finite map vertex");
   r.lo.x=std::min(r.lo.x,p.x);r.lo.y=std::min(r.lo.y,p.y);
   r.hi.x=std::max(r.hi.x,p.x);r.hi.y=std::max(r.hi.y,p.y);
  }
  r.map=std::move(map);next.push_back(std::move(r));
 }
 regions=std::move(next);horizontalTiles.clear();
}
bool MapSource::heights(int sector,float floor,float ceiling){
 if(!std::isfinite(floor)||!std::isfinite(ceiling))return false;
 for(auto& r:regions)if(r.map.sector==sector){
  r.map.floor=floor;r.map.ceiling=ceiling;return true;
 }
 return false;
}
float MapSource::horizontal(const Region& r,float x,float y){
 float nearest=std::numeric_limits<float>::max();bool inside=false;
 // Even/odd membership supports concave outlines, multiple islands and holes.
 // Self-referencing lines must be excluded by the engine adapter.
 for(const auto& edge:r.map.edges){
  float dx=edge.b.x-edge.a.x,dy=edge.b.y-edge.a.y;
  float len=dx*dx+dy*dy;
  float t=len>0?std::clamp(((x-edge.a.x)*dx+(y-edge.a.y)*dy)/len,0.f,1.f):0;
  nearest=std::min(nearest,std::hypot(x-edge.a.x-t*dx,y-edge.a.y-t*dy));
  if((edge.a.y>y)!=(edge.b.y>y))
   if(x<edge.a.x+(y-edge.a.y)*dx/dy)inside=!inside;
 }
 return inside?-nearest:nearest;
}
float MapSource::sample(Vec p)const{
 float value=step*2;
 for(const auto& r:regions){
  if(r.map.ceiling<=r.map.floor)continue;
  // Only a narrow distance band is required by the mesher.
  if(p.x<r.lo.x-value||p.x>r.hi.x+value||
     p.y<r.lo.y-value||p.y>r.hi.y+value)continue;
  float vertical=std::max(r.map.floor-p.z,p.z-r.map.ceiling);
  if(vertical>value)continue;
  value=std::min(value,std::max(horizontal(r,p.x,p.y),vertical));
 }
 // An exact shared sector boundary belongs to air, not a spurious wall.
 return std::abs(value)<.001f?-.001f:std::clamp(value,-step*2,step*2);
}
float MapSource::depth(Vec p)const{
 float value=1e9f;
 for(const auto& r:regions)if(r.map.ceiling>r.map.floor)
  value=std::min(value,std::max(horizontal(r,p.x,p.y),std::max(r.map.floor-p.z,p.z-r.map.ceiling)));
 return value;
}
Volume MapSource::tile(int x,int y,int z,bool mesh)const{
 // Limit coordinates before integer-to-world multiplication.
 if(std::abs(double(x))>1048576||std::abs(double(y))>1048576||std::abs(double(z))>1048576)
  throw std::invalid_argument("Tile coordinate out of range");
 Volume out;out.create(tileCells+1,tileCells+1,tileCells+1,step,
  {float(x)*tileCells*step,float(y)*tileCells*step,float(z)*tileCells*step});
 // Lift/door motion changes vertical intervals only. Reuse the immutable
 // outline distances across heights and vertically adjacent voxel tiles.
 const std::array<int,2> key{x,y};
 if(horizontalTiles.size()>=2048&&!horizontalTiles.count(key))horizontalTiles.clear();
 auto inserted=horizontalTiles.try_emplace(key);
 auto& columns=inserted.first->second;
 Vec end=out.point(out.nx-1,out.ny-1,out.nz-1);
 if(inserted.second)for(size_t n=0;n<regions.size();++n){
  const auto& r=regions[n];
  if(r.hi.x<out.origin.x-step*2||r.lo.x>end.x+step*2||
     r.hi.y<out.origin.y-step*2||r.lo.y>end.y+step*2)continue;
  ColumnRegion column;column.region=n;
  for(int iy=0;iy<out.ny;++iy)for(int ix=0;ix<out.nx;++ix){
   Vec p=out.point(ix,iy,0);
   column.distances[iy*out.nx+ix]=(p.x<r.lo.x-step*2||p.x>r.hi.x+step*2||
      p.y<r.lo.y-step*2||p.y>r.hi.y+step*2)?step*2:horizontal(r,p.x,p.y);
  }
  columns.push_back(std::move(column));
 }
 std::vector<const ColumnRegion*> nearby;
 for(const auto& column:columns){const auto& r=regions[column.region].map;
  if(r.ceiling>r.floor&&r.ceiling>=out.origin.z-step*2&&r.floor<=end.z+step*2)nearby.push_back(&column);
 }
 for(int iy=0;iy<out.ny;++iy)for(int ix=0;ix<out.nx;++ix){
  for(int iz=0;iz<out.nz;++iz){
   float pz=out.point(ix,iy,iz).z,value=step*2;
   for(size_t i=0;i<nearby.size();++i){
    const auto& r=regions[nearby[i]->region].map;
    if(r.ceiling<=r.floor)continue;
    value=std::min(value,std::max(nearby[i]->distances[iy*out.nx+ix],std::max(r.floor-pz,pz-r.ceiling)));
   }
   value=std::abs(value)<.001f?-.001f:std::clamp(value,-step*2,step*2);
   out.base[out.index(ix,iy,iz)]=out.density[out.index(ix,iy,iz)]=value;
  }
 }
 if(mesh)out.rebuild();return out;
}
}
