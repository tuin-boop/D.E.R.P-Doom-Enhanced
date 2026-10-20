// Replace only edited world tiles. Original map faces outside these boxes
// retain their texture coordinates, lighting and material.
static void buildSolidMapMesh(){
 const auto* world=SV_MapWorld();if(!world||!world->cutCount())return;
 struct Face {int material;Vertex a,b,c;lab::Vec normal;};
 struct Cached {unsigned revision=~0u;std::map<int,std::vector<Vertex>> vertices;};
 struct SkinCache {uint64_t revision=0;std::vector<Vertex> source,output;};
 static unsigned level=~0u;static std::map<lab::TileKey,Cached> cached;
 static std::map<uint64_t,SkinCache> skins;
 static std::map<int,SkinCache> batchSkins;
 static std::map<uint64_t,SkinCache> patches;
 struct DamageBounds {unsigned revision=~0u;lab::Vec lo,hi;};
 static std::map<lab::TileKey,DamageBounds> damageBounds;
 if(level!=r_levelserial){level=r_levelserial;cached.clear();skins.clear();batchSkins.clear();patches.clear();damageBounds.clear();}
 auto pos=[](const Vertex& v){return lab::Vec{v.x,v.y,v.z};};
 auto interpolate=[](Vertex a,const Vertex& b,float t){
  auto lerp=[&](float x,float y){return x+(y-x)*t;};
  a.x=lerp(a.x,b.x);a.y=lerp(a.y,b.y);a.z=lerp(a.z,b.z);
  a.u=lerp(a.u,b.u);a.v=lerp(a.v,b.v);a.light=lerp(a.light,b.light);
  a.red=lerp(a.red,b.red);a.green=lerp(a.green,b.green);a.blue=lerp(a.blue,b.blue);
  a.sunU=lerp(a.sunU,b.sunU);a.sunV=lerp(a.sunV,b.sunV);return a;
 };
 auto overlap=[](const Face& f,lab::Vec lo,lab::Vec hi){
  return std::max({f.a.x,f.b.x,f.c.x})>=lo.x&&std::min({f.a.x,f.b.x,f.c.x})<=hi.x&&
   std::max({f.a.y,f.b.y,f.c.y})>=lo.y&&std::min({f.a.y,f.b.y,f.c.y})<=hi.y&&
   std::max({f.a.z,f.b.z,f.c.z})>=lo.z&&std::min({f.a.z,f.b.z,f.c.z})<=hi.z;
 };
 int rock=W_CheckNumForName((char*)"DIGROCK");
 int concrete=rock>=0?-1-rock:R_TextureNumForName((char*)"GRAY1");
 if(rock>=0)lumpImage(rock,true);else wallImage(concrete);
 // Keep the original faces and cut only the opening, rather than replacing
 // an entire tile with an approximation of its textures and light bakes.
 std::vector<std::pair<lab::Vec,lab::Vec>> boxes;
 std::vector<unsigned> boxRevisions;
 uint64_t revision=1469598103934665603ULL;
 for(const auto& entry:world->tiles())if(entry.second.edited){
  auto& bounds=damageBounds[entry.first];const auto& volume=entry.second.volume;
  if(bounds.revision!=entry.second.revision){
   bounds.revision=entry.second.revision;
   bounds.lo=volume.point(16,16,16);bounds.hi=volume.origin;
   for(int z=0;z<volume.nz;++z)for(int y=0;y<volume.ny;++y)for(int x=0;x<volume.nx;++x){
    auto i=volume.index(x,y,z);if(volume.density[i]>=volume.base[i]-.01f)continue;
    auto p=volume.point(x,y,z);
    bounds.lo={std::min(bounds.lo.x,p.x-4),std::min(bounds.lo.y,p.y-4),std::min(bounds.lo.z,p.z-4)};
    bounds.hi={std::max(bounds.hi.x,p.x+4),std::max(bounds.hi.y,p.y+4),std::max(bounds.hi.z,p.z+4)};
   }
  }
  boxes.push_back({bounds.lo,bounds.hi});
  boxRevisions.push_back(entry.second.revision);
  revision=(revision^entry.second.revision)*1099511628211ULL;
 }
 if(skins.size()>30000)skins.clear();
 if(patches.size()>100000)patches.clear();
 for(auto& batch:batches){
  auto& whole=batchSkins[batch.first];
  if(whole.revision==revision&&whole.source.size()==batch.second.size()&&
     (batch.second.empty()||!memcmp(whole.source.data(),batch.second.data(),batch.second.size()*sizeof(Vertex)))){
   batch.second=whole.output;continue;
  }
  whole.source=batch.second;whole.revision=revision;
  std::vector<Vertex> output;output.reserve(batch.second.size());
  auto carve=[&](auto&& self,Vertex a,Vertex b,Vertex c,int depth,const std::vector<size_t>& candidates)->void{
   Face face{batch.first,a,b,c,{}};std::vector<size_t> nearby;
   for(size_t i:candidates)if(overlap(face,boxes[i].first,boxes[i].second))nearby.push_back(i);
   if(nearby.empty()){output.insert(output.end(),{a,b,c});return;}
   float ab=lab::length(pos(a)-pos(b)),bc=lab::length(pos(b)-pos(c)),ca=lab::length(pos(c)-pos(a));
   float edge=std::max({ab,bc,ca});
   if(edge>16&&depth<32){
    // Long, thin door strips must not subdivide in both directions until
    // their short edge is thousands of times smaller than a field cell.
    if(ab>=bc&&ab>=ca){Vertex middle=interpolate(a,b,.5f);self(self,a,middle,c,depth+1,nearby);self(self,middle,b,c,depth+1,nearby);}
    else if(bc>=ca){Vertex middle=interpolate(b,c,.5f);self(self,a,b,middle,depth+1,nearby);self(self,a,middle,c,depth+1,nearby);}
    else {Vertex middle=interpolate(c,a,.5f);self(self,a,b,middle,depth+1,nearby);self(self,middle,b,c,depth+1,nearby);}
    return;
   }
   Vertex source[3]={a,b,c};uint64_t patchHash=uint64_t(batch.first);
   const auto* patchBytes=reinterpret_cast<const unsigned char*>(source);
   for(size_t i=0;i<sizeof(source);++i)patchHash=(patchHash^patchBytes[i])*1099511628211ULL;
   uint64_t stamp=world->skinStamp({std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})},
                                 {std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})});
   auto& patch=patches[patchHash];
   if(patch.revision==stamp&&patch.source.size()==3&&!memcmp(patch.source.data(),source,sizeof(source))){
    output.insert(output.end(),patch.output.begin(),patch.output.end());return;
   }
   size_t patchStart=output.size();
   for(const auto& polygon:lab::partitionSkin(a,b,c,pos,interpolate)){
   std::vector<Vertex> kept;Vertex previous=polygon.back();float pd=world->removal(pos(previous));
   for(Vertex current:polygon){
    float cd=world->removal(pos(current));
    if((pd<=0)!=(cd<=0))kept.push_back(interpolate(previous,current,pd/(pd-cd)));
    if(cd<=0)kept.push_back(current);previous=current;pd=cd;
   }
   for(size_t n=1;n+1<kept.size();++n){
    output.insert(output.end(),{kept[0],kept[n],kept[n+1]});
   }
   }
   patch.revision=stamp;patch.source.assign(source,source+3);
   patch.output.assign(output.begin()+patchStart,output.end());
  };
  for(size_t i=0;i+2<batch.second.size();i+=3){
   const Vertex* vertices=batch.second.data()+i;
   Face face{batch.first,vertices[0],vertices[1],vertices[2],{}};
   std::vector<size_t> candidates;uint64_t localRevision=1469598103934665603ULL;
   for(size_t j=0;j<boxes.size();++j)if(overlap(face,boxes[j].first,boxes[j].second)){
    candidates.push_back(j);localRevision=(localRevision^boxRevisions[j])*1099511628211ULL;
   }
   if(candidates.empty()){output.insert(output.end(),vertices,vertices+3);continue;}
   uint64_t hash=uint64_t(batch.first);
   const auto* bytes=reinterpret_cast<const unsigned char*>(vertices);
   for(size_t j=0;j<3*sizeof(Vertex);++j)hash=(hash^bytes[j])*1099511628211ULL;
   auto& skin=skins[hash];
   if(skin.revision==localRevision&&skin.source.size()==3&&!memcmp(skin.source.data(),vertices,3*sizeof(Vertex))){
    output.insert(output.end(),skin.output.begin(),skin.output.end());continue;
   }
   size_t start=output.size();
   carve(carve,vertices[0],vertices[1],vertices[2],0,candidates);
   skin.revision=localRevision;skin.source.assign(vertices,vertices+3);
   skin.output.assign(output.begin()+start,output.end());
  }
  whole.output=output;batch.second=std::move(output);
 }
 for(const auto& entry:world->tiles()){
  const auto& tile=entry.second;if(!tile.edited)continue;
  lab::Vec lo=tile.volume.origin,hi=tile.volume.point(16,16,16);
  auto& cache=cached[entry.first];
  if(cache.revision!=tile.revision){
   cache.revision=tile.revision;cache.vertices.clear();
   for(const auto& chunk:tile.volume.chunks)for(const auto& triangle:chunk.triangles){
    if(!triangle.damaged)continue;
    // Split at the original skin: concrete belongs only to newly exposed
    // material, never to the intact grid cells surrounding the crater.
    std::vector<lab::Vec> polygon;lab::Vec previous=triangle.c;
    // End inside the skin's retained numerical lip, so rounding cannot
    // expose a gap between the two independently generated surfaces.
    float pd=tile.volume.field(previous,true)-.00001f;
    for(lab::Vec current:{triangle.a,triangle.b,triangle.c}){
     float cd=tile.volume.field(current,true)-.00001f;
     if((pd>=0)!=(cd>=0))polygon.push_back(previous+(current-previous)*(pd/(pd-cd)));
     if(cd>=0)polygon.push_back(current);previous=current;pd=cd;
    }
    if(polygon.size()<3)continue;
    auto midpoint=(triangle.a+triangle.b+triangle.c)*(1.f/3);
    const sector_t* owning=R_PointInSubsector((fixed_t)(midpoint.x*FRACUNIT),(fixed_t)(midpoint.y*FRACUNIT))->sector;
    auto vertex=[&](lab::Vec p){
     Vertex v{};v.x=p.x;v.y=p.y;v.z=p.z;v.mode=sectorMode(owning);
     bool flat=std::abs(triangle.normal.z)>std::max(std::abs(triangle.normal.x),std::abs(triangle.normal.y));
     v.u=flat?p.x:std::abs(triangle.normal.x)>std::abs(triangle.normal.y)?p.y:p.x;
     v.v=flat?-p.y:-p.z;
     float facet=.7f+.22f*std::abs(triangle.normal.z)+.08f*std::abs(triangle.normal.x);
     v.light=facet*(.58f+.42f*std::min(1.f,std::max(0.f,tile.volume.field(p,true))/1.5f));
     return v;
    };
    auto& out=cache.vertices[concrete];
    for(size_t n=1;n+1<polygon.size();++n){
     Vertex a=vertex(polygon[0]),b=vertex(polygon[n]),c=vertex(polygon[n+1]);
     excavationNormal(a,b,c);out.insert(out.end(),{a,b,c});
    }
   }
  }
 }
 // Add replacements after clipping ALL tiles; otherwise later boxes would
 // clip the replacement meshes of earlier neighbours.
 for(const auto& entry:world->tiles())if(entry.second.edited)
  for(const auto& batch:cached[entry.first].vertices)
   batches[batch.first].insert(batches[batch.first].end(),batch.second.begin(),batch.second.end());
}
