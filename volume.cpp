#include "volume.h"
#include <fstream>
#include <algorithm>
#include <limits>
#include <cstring>
namespace lab {
static constexpr int cells=16;
void Volume::create(int x,int y,int z,float step,Vec start){
 nx=x;ny=y;nz=z;spacing=step;origin=start;base.assign(size_t(x)*y*z,1);density=base;setupChunks();
}
void Volume::setupChunks(){
 cx=(nx-2)/cells+1;cy=(ny-2)/cells+1;cz=(nz-2)/cells+1;chunks.clear();
 for(int z=0;z<cz;++z)for(int y=0;y<cy;++y)for(int x=0;x<cx;++x)
  chunks.push_back({point(x*cells,y*cells,z*cells),point(std::min(nx-1,(x+1)*cells),std::min(ny-1,(y+1)*cells),std::min(nz-1,(z+1)*cells)),{},true});
}
bool Volume::save(const std::string &path)const{
 std::ofstream f(path,std::ios::binary);if(!f)return false;
 return save(f);
}
bool Volume::save(std::ostream &f)const{
 f.write("SVL1",4);int dims[3]={nx,ny,nz};f.write((char*)dims,sizeof dims);
 f.write((char*)&spacing,4);f.write((char*)&origin,sizeof origin);f.write((char*)&spawn,sizeof spawn);f.write((char*)&yaw,4);
 f.write((char*)base.data(),base.size()*4);f.write((char*)density.data(),density.size()*4);return bool(f);
}
bool Volume::load(const std::string &path){
 std::ifstream f(path,std::ios::binary);return load(f);
}
bool Volume::load(std::istream &f){
 char magic[4];int d[3];float step,angle;Vec start,position;
 f.read(magic,4);f.read((char*)d,sizeof d);f.read((char*)&step,4);f.read((char*)&start,sizeof start);f.read((char*)&position,sizeof position);f.read((char*)&angle,4);
 if(!f||std::memcmp(magic,"SVL1",4)||d[0]<2||d[1]<2||d[2]<2||d[0]>512||d[1]>512||d[2]>512||!std::isfinite(step)||step<.25f||step>32||
 !std::isfinite(start.x)||!std::isfinite(start.y)||!std::isfinite(start.z)||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)||!std::isfinite(angle))return false;
 size_t n=size_t(d[0])*d[1]*d[2];if(n>16000000)return false;
 std::vector<float> b(n),v(n);f.read((char*)b.data(),n*4);f.read((char*)v.data(),n*4);if(!f)return false;
 for(size_t i=0;i<n;++i)if(!std::isfinite(b[i])||!std::isfinite(v[i]))return false;
 nx=d[0];ny=d[1];nz=d[2];spacing=step;origin=start;spawn=position;yaw=angle;base=std::move(b);density=std::move(v);setupChunks();++revision;rebuild();return true;
}
static bool boxRay(Vec start,Vec dir,Vec lo,Vec hi,float max){
 float low=0,high=max;
 const float a[3]={start.x,start.y,start.z},b[3]={dir.x,dir.y,dir.z},l[3]={lo.x,lo.y,lo.z},h[3]={hi.x,hi.y,hi.z};
 for(int i=0;i<3;++i){if(std::abs(b[i])<1e-8f){if(a[i]<l[i]||a[i]>h[i])return false;}
 else {float t=(l[i]-a[i])/b[i],u=(h[i]-a[i])/b[i];if(t>u)std::swap(t,u);low=std::max(low,t);high=std::min(high,u);if(high<low)return false;}}
 return true;
}
float Volume::field(Vec p,bool original)const{
 float q[3]={(p.x-origin.x)/spacing,(p.y-origin.y)/spacing,(p.z-origin.z)/spacing};
 int size[3]={nx,ny,nz},cell[3];float f[3];int axis[3]={0,1,2};
 for(int i=0;i<3;++i){cell[i]=std::clamp(int(std::floor(q[i])),0,size[i]-2);f[i]=std::clamp(q[i]-cell[i],0.f,1.f);}
 std::sort(axis,axis+3,[&](int a,int b){return f[a]>f[b];});
 const auto& values=original?base:density;
 auto at=[&](){return values[index(cell[0],cell[1],cell[2])];};
 float result=at()*(1-f[axis[0]]);
 ++cell[axis[0]];result+=at()*(f[axis[0]]-f[axis[1]]);
 ++cell[axis[1]];result+=at()*(f[axis[1]]-f[axis[2]]);
 ++cell[axis[2]];return result+at()*f[axis[2]];
}
void Volume::rebuild(){
 lastRebuilt=0;
 const int corners[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
 // A single consistent tetrahedralization across every cell and chunk.
 const int tetra[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
 for(int iz=0;iz<cz;++iz)for(int iy=0;iy<cy;++iy)for(int ix=0;ix<cx;++ix){
  Chunk &chunk=chunks[(iz*cy+iy)*cx+ix];if(!chunk.dirty)continue;chunk.dirty=false;chunk.triangles.clear();++chunk.revision;++lastRebuilt;
  for(int z=iz*cells;z<std::min(nz-1,(iz+1)*cells);++z)for(int y=iy*cells;y<std::min(ny-1,(iy+1)*cells);++y)for(int x=ix*cells;x<std::min(nx-1,(ix+1)*cells);++x){
   Vec p[8];float v[8];size_t ids[8];bool damage=false,hasAir=false,hasSolid=false;
   for(int i=0;i<8;++i){int xx=x+corners[i][0],yy=y+corners[i][1],zz=z+corners[i][2];size_t k=index(xx,yy,zz);ids[i]=k;p[i]=point(xx,yy,zz);v[i]=density[k];hasAir|=v[i]<0;hasSolid|=v[i]>=0;damage|=density[k]<base[k]-excavationEpsilon;}
   if(!hasAir||!hasSolid)continue;
   // Shared edges must execute identical floating-point arithmetic, even
   // when adjacent tetrahedra traverse that edge in opposite directions.
   auto edge=[&](int a,int b){if(ids[a]>ids[b])std::swap(a,b);return p[a]+(p[b]-p[a])*(v[a]/(v[a]-v[b]));};
   for(auto &t:tetra){int in[4],out[4],ni=0,no=0;Vec air{},solid{};
    for(int i:t)if(v[i]<0){in[ni++]=i;air=air+p[i];}else{out[no++]=i;solid=solid+p[i];}
    if(!ni||!no)continue;Vec facing=air*(1.f/ni)-solid*(1.f/no);
    auto emit=[&](Vec a,Vec b,Vec c){Vec n=cross(b-a,c-a);if(length(n)<1e-7f)return;if(dot(n,facing)<0){std::swap(b,c);n=n*-1;}chunk.triangles.push_back({a,b,c,normalized(n),damage});};
    if(ni==1)emit(edge(in[0],out[0]),edge(in[0],out[1]),edge(in[0],out[2]));
    else if(ni==3)emit(edge(out[0],in[0]),edge(out[0],in[2]),edge(out[0],in[1]));
    else {Vec a=edge(in[0],out[0]),b=edge(in[0],out[1]),c=edge(in[1],out[0]),d=edge(in[1],out[1]);emit(a,b,c);emit(b,d,c);}
   }
  }
 }
}
int Volume::removeSmallFragments(int maxSamples){
 if(maxSamples<=0)return 0;
 std::vector<unsigned char> seen(density.size(),0);
 std::vector<size_t> component;int removed=0;
 // Edges of the same tetrahedral subdivision used by rebuild(), including
 // face/body diagonals. Six-neighbour connectivity would sever valid joins.
 const int edges[7][3]={{1,0,0},{0,1,0},{0,0,1},{1,1,0},{1,0,1},{0,1,1},{1,1,1}};
 for(size_t seed=0;seed<density.size();++seed){
  if(seen[seed]||density[seed]<0)continue;
  component.clear();component.push_back(seed);seen[seed]=1;bool boundary=false;
  for(size_t cursor=0;cursor<component.size();++cursor){
   size_t at=component[cursor];int x=int(at%nx),y=int(at/nx%ny),z=int(at/(size_t(nx)*ny));
   boundary|=x==0||y==0||z==0||x==nx-1||y==ny-1||z==nz-1;
   for(auto &edge:edges)for(int sign:{-1,1}){
    int xx=x+edge[0]*sign,yy=y+edge[1]*sign,zz=z+edge[2]*sign;
    if(xx<0||yy<0||zz<0||xx>=nx||yy>=ny||zz>=nz)continue;
    size_t next=index(xx,yy,zz);
    if(!seen[next]&&density[next]>=0){seen[next]=1;component.push_back(next);}
   }
  }
  if(boundary||component.size()>size_t(maxSamples))continue;
  for(size_t at:component){
   density[at]=-spacing;int x=int(at%nx),y=int(at/nx%ny),z=int(at/(size_t(nx)*ny));++removed;
   for(int zz=std::max(0,(z-1)/cells);zz<=std::min(cz-1,z/cells);++zz)
    for(int yy=std::max(0,(y-1)/cells);yy<=std::min(cy-1,y/cells);++yy)
     for(int xx=std::max(0,(x-1)/cells);xx<=std::min(cx-1,x/cells);++xx)chunks[(zz*cy+yy)*cx+xx].dirty=true;
  }
 }
 if(removed){++revision;rebuild();}return removed;
}
bool Volume::blast(Vec center,float radius){
 if(!std::isfinite(radius)||radius<=0)return false;bool changed=false;
 int lo[3],hi[3];float a[3]={center.x-origin.x,center.y-origin.y,center.z-origin.z};int dims[3]={nx,ny,nz};
 for(int k=0;k<3;++k){lo[k]=std::max(1,int(std::floor((a[k]-radius*1.2f)/spacing)));hi[k]=std::min(dims[k]-2,int(std::ceil((a[k]+radius*1.2f)/spacing)));}
 for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x){
  Vec p=point(x,y,z),d=p-center;
  float fractured=radius*(1+.055f*std::sin(p.x*.31f+p.z*.27f)*std::cos(p.y*.29f-p.z*.17f));
  size_t k=index(x,y,z);float value=length(d)-fractured;
  if(value>=density[k])continue;density[k]=value;changed=true;
  // A changed sample belongs to cells on both sides, including chunk seams.
  for(int zz=std::max(0,(z-1)/cells);zz<=std::min(cz-1,z/cells);++zz)
   for(int yy=std::max(0,(y-1)/cells);yy<=std::min(cy-1,y/cells);++yy)
    for(int xx=std::max(0,(x-1)/cells);xx<=std::min(cx-1,x/cells);++xx)chunks[(zz*cy+yy)*cx+xx].dirty=true;
 }
 if(changed){++revision;rebuild();}return changed;
}
bool Volume::ray(Vec start,Vec dir,float range,Hit &hit)const{
 dir=normalized(dir);bool found=false;hit.distance=range;
 for(const Chunk &chunk:chunks){if(!boxRay(start,dir,chunk.lo,chunk.hi,hit.distance))continue;
  for(const Triangle &t:chunk.triangles){Vec e=t.b-t.a,f=t.c-t.a,h=cross(dir,f);float det=dot(e,h);if(std::abs(det)<1e-8f)continue;
   float inv=1/det;Vec s=start-t.a;float u=inv*dot(s,h);if(u<-.00001f||u>1.00001f)continue;
   Vec q=cross(s,e);float v=inv*dot(dir,q);if(v<-.00001f||u+v>1.00001f)continue;
   float dist=inv*dot(f,q);if(dist<.001f||dist>=hit.distance)continue;
   found=true;hit={dist,start+dir*dist,t.normal};
  }
 }return found;
}
static Vec closest(Vec p,const Triangle&t){
 Vec ab=t.b-t.a,ac=t.c-t.a,ap=p-t.a;float d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return t.a;
 Vec bp=p-t.b;float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return t.b;
 float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return t.a+ab*(d1/(d1-d3));
 Vec cp=p-t.c;float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return t.c;
 float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return t.a+ac*(d2/(d2-d6));
 float va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)return t.b+(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6)));
 float denom=1/(va+vb+vc);return t.a+ab*(vb*denom)+ac*(vc*denom);
}
static float segmentDistance(Vec p,Vec q,Vec a,Vec b){
 Vec d=q-p,e=b-a,r=p-a;float aa=dot(d,d),ee=dot(e,e),f=dot(e,r),s=0,t=0;
 if(aa<=1e-10f&&ee<=1e-10f)return dot(r,r);
 if(aa<=1e-10f)t=std::clamp(f/ee,0.f,1.f);
 else {float c=dot(d,r);
  if(ee<=1e-10f)s=std::clamp(-c/aa,0.f,1.f);
  else {float k=dot(d,e),den=aa*ee-k*k;s=den>1e-10f?std::clamp((k*f-c*ee)/den,0.f,1.f):0;t=(k*s+f)/ee;
   if(t<0){t=0;s=std::clamp(-c/aa,0.f,1.f);}else if(t>1){t=1;s=std::clamp((k-c)/aa,0.f,1.f);}
  }
 }
 Vec delta=(p+d*s)-(a+e*t);return dot(delta,delta);
}
static float capsuleDistance(Vec p,Vec q,const Triangle&t){
 Vec d=q-p,e=t.b-t.a,f=t.c-t.a,h=cross(d,f);float det=dot(e,h);
 if(std::abs(det)>1e-8f){float inv=1/det;Vec s=p-t.a;float u=inv*dot(s,h);Vec r=cross(s,e);float v=inv*dot(d,r),at=inv*dot(f,r);
  if(u>=0&&v>=0&&u+v<=1&&at>=0&&at<=1)return 0;
 }
 Vec a=p-closest(p,t),b=q-closest(q,t);
 return std::min({dot(a,a),dot(b,b),segmentDistance(p,q,t.a,t.b),segmentDistance(p,q,t.b,t.c),segmentDistance(p,q,t.c,t.a)});
}
bool Volume::fits(Vec feet,float radius,float height,bool bounded)const{
 Vec hi=point(nx-1,ny-1,nz-1);
 if(bounded&&(feet.x-radius<origin.x||feet.x+radius>hi.x||feet.y-radius<origin.y||feet.y+radius>hi.y||feet.z<origin.z||feet.z+height>hi.z))return false;
 // Exact capsule/triangle distance against the same mesh used for drawing.
 for(const auto &chunk:chunks){
  if(feet.x+radius<chunk.lo.x||feet.x-radius>chunk.hi.x||feet.y+radius<chunk.lo.y||feet.y-radius>chunk.hi.y||feet.z+height<chunk.lo.z||feet.z>chunk.hi.z)continue;
  for(const auto&t:chunk.triangles){
   if(std::max({t.a.x,t.b.x,t.c.x})<feet.x-radius||
      std::min({t.a.x,t.b.x,t.c.x})>feet.x+radius||
      std::max({t.a.y,t.b.y,t.c.y})<feet.y-radius||
      std::min({t.a.y,t.b.y,t.c.y})>feet.y+radius||
      std::max({t.a.z,t.b.z,t.c.z})<feet.z||
      std::min({t.a.z,t.b.z,t.c.z})>feet.z+height)continue;
   if(capsuleDistance(feet+Vec{0,0,radius},feet+Vec{0,0,height-radius},t)<(radius-.015f)*(radius-.015f))return false;
  }
 }
 // Surface tests alone cannot detect a capsule wholly inside solid material.
 Vec p=feet+Vec{0,0,height*.5f};
 if(!bounded&&(p.x<origin.x||p.y<origin.y||p.z<origin.z||p.x>hi.x||p.y>hi.y||p.z>hi.z))return true;
 int x=std::clamp(int(std::round((p.x-origin.x)/spacing)),0,nx-1),y=std::clamp(int(std::round((p.y-origin.y)/spacing)),0,ny-1),z=std::clamp(int(std::round((p.z-origin.z)/spacing)),0,nz-1);
 return density[index(x,y,z)]<=0;
}
Vec Volume::move(Vec feet,Vec delta,float radius,float height)const{
 int steps=std::max(1,int(std::ceil(length(delta)/2)));Vec step=delta*(1.f/steps);
 for(int i=0;i<steps;++i){
  if(fits(feet+step,radius,height)){feet=feet+step;continue;}
  Vec old=feet;
  for(Vec s:{Vec{step.x,0,0},Vec{0,step.y,0},Vec{0,0,step.z}})if(fits(feet+s,radius,height))feet=feet+s;
  if(dot(feet-old,feet-old)<1e-12f)break;
 }return feet;
}
size_t Volume::triangleCount()const{size_t n=0;for(const auto&c:chunks)n+=c.triangles.size();return n;}
}
