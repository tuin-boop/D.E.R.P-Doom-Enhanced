#pragma once
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <iosfwd>
namespace lab {
struct Vec {
 float x=0,y=0,z=0;
 Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
 Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
 Vec operator*(float s)const{return {x*s,y*s,z*s};}
};
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(Vec a){return std::sqrt(dot(a,a));}
inline Vec normalized(Vec a){float l=length(a);return l>1e-8f?a*(1/l):Vec{};}
struct Triangle {Vec a,b,c,normal;bool damaged=false;};
struct Chunk {Vec lo,hi;std::vector<Triangle> triangles;bool dirty=true;unsigned revision=0;};
struct Hit {float distance;Vec point,normal;};
class Volume {
public:
 int nx=0,ny=0,nz=0;
 float spacing=4;
 Vec origin,spawn;float yaw=0;
 std::vector<float> base,density;
 std::vector<Chunk> chunks;
 unsigned revision=0;int lastRebuilt=0;
 void create(int x,int y,int z,float step,Vec start);
 bool load(const std::string& path);
 bool save(const std::string& path)const;
 bool load(std::istream& stream);
 bool save(std::ostream& stream)const;
 size_t index(int x,int y,int z)const{return (size_t(z)*ny+y)*nx+x;}
 Vec point(int x,int y,int z)const{return origin+Vec{float(x),float(y),float(z)}*spacing;}
 void rebuild();
 float field(Vec point,bool original=false)const;
 bool blast(Vec center,float radius);
 int removeSmallFragments(int maxSamples=4);
 bool ray(Vec start,Vec direction,float range,Hit &hit)const;
 bool fits(Vec feet,float radius=12,float height=56,bool bounded=true)const;
 Vec move(Vec feet,Vec delta,float radius=12,float height=56)const;
 size_t triangleCount()const;
private:
 int cx=0,cy=0,cz=0;
 void setupChunks();
};
}
