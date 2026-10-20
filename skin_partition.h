#pragma once
#include "volume.h"
#include <algorithm>
namespace lab {
// A skin polygon must not interpolate a sampled field across tetrahedra.
// Split on the same cube and fractional-coordinate ordering planes as field().
template<class V,class Position,class Interpolate>
std::vector<std::vector<V>> partitionSkin(V a,V b,V c,Position position,Interpolate interpolate){
 std::vector<std::vector<V>> pieces{{a,b,c}};
 for(Vec normal:{Vec{1,0,0},Vec{0,1,0},Vec{0,0,1},Vec{1,-1,0},Vec{1,0,-1},Vec{0,1,-1}}){
  float low=std::min({dot(position(a),normal),dot(position(b),normal),dot(position(c),normal)});
  float high=std::max({dot(position(a),normal),dot(position(b),normal),dot(position(c),normal)});
  for(float plane=(std::floor(low/4)+1)*4;plane<high-.0001f;plane+=4){
   std::vector<std::vector<V>> next;
   for(const auto& polygon:pieces){
    float lo=1e30f,hi=-1e30f;
    for(const auto& v:polygon){float d=dot(position(v),normal)-plane;lo=std::min(lo,d);hi=std::max(hi,d);}
    if(lo>=-.0001f||hi<=.0001f){next.push_back(polygon);continue;}
    std::vector<V> front,back;V previous=polygon.back();
    float pd=dot(position(previous),normal)-plane;
    for(const auto& current:polygon){
     float cd=dot(position(current),normal)-plane;
     if((pd<0&&cd>0)||(pd>0&&cd<0)){
      V crossing=interpolate(previous,current,pd/(pd-cd));front.push_back(crossing);back.push_back(crossing);
     }
     if(cd>=0)front.push_back(current);
     if(cd<=0)back.push_back(current);
     previous=current;pd=cd;
    }
    if(front.size()>=3)next.push_back(std::move(front));
    if(back.size()>=3)next.push_back(std::move(back));
   }
   pieces=std::move(next);
  }
 }
 return pieces;
}
}
