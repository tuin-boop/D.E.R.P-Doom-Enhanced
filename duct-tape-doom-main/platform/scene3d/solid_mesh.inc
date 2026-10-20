static bool buildSolidScene(float fraction){
 const auto *scene=SV_Scene();if(!scene)return false;
 const bool profile=M_CheckParm((char*)"-profilesolid")!=0;
 uint64_t started=profile?SDL_GetTicksNS():0;
 bool rebuilt=false;
 const int skin=R_TextureNumForName((char*)"STARTAN3"),doorTex=R_TextureNumForName((char*)"BIGDOOR2"),concrete=R_TextureNumForName((char*)"GRAY1");
 wallImage(skin);wallImage(doorTex);wallImage(concrete);
 int floor=-1-W_GetNumForName((char*)"FLOOR0_1"),ceiling=-1-W_GetNumForName((char*)"CEIL3_5");
 lumpImage(-1-floor,true);lumpImage(-1-ceiling,true);
 struct ChunkCache {unsigned revision=~0u;std::map<int,std::vector<Vertex>> vertices;};
 struct Cache {unsigned level=~0u,mode=~0u;std::vector<ChunkCache> chunks;};
 static Cache terrainCache,doorCache;
 auto draw=[&](const lab::Volume& volume,float offset,bool moving,Cache& cache){
  unsigned mode=sectorMode(&sectors[0]);
  if(cache.level!=r_levelserial||cache.mode!=mode||cache.chunks.size()!=volume.chunks.size()){
   cache.level=r_levelserial;cache.mode=mode;cache.chunks.clear();cache.chunks.resize(volume.chunks.size());
  }
  for(size_t n=0;n<volume.chunks.size();++n){
   const auto& chunk=volume.chunks[n];auto& stored=cache.chunks[n];
   if(stored.revision!=chunk.revision){
    rebuilt=true;stored.revision=chunk.revision;
    // Retain allocations across subsequent impacts in this chunk.
    for(auto& entry:stored.vertices)entry.second.clear();
    for(const auto &t:chunk.triangles){
     bool flat=std::abs(t.normal.z)>.8f;
     int material=t.damaged?concrete:moving?doorTex:flat?(t.normal.z>0?floor:ceiling):skin;
     auto &out=stored.vertices[material];
     float light=.65f+.25f*std::abs(t.normal.z)+.1f*std::abs(t.normal.x);
     for(auto p:{t.a,t.b,t.c}){
      float u=flat?p.x:std::abs(t.normal.x)>std::abs(t.normal.y)?p.y:p.x;
      float v=flat?-p.y:-p.z;
      out.push_back({p.x,p.y,p.z,u,v,light,mode});
     }
    }
   }
   for(const auto& entry:stored.vertices){
    auto &out=batches[entry.first];size_t first=out.size();
    out.insert(out.end(),entry.second.begin(),entry.second.end());
    if(offset!=0)for(size_t i=first;i<out.size();++i)out[i].z+=offset;
   }
  }
 };
 draw(scene->terrain,0,false,terrainCache);
 if(scene->hasDoor)draw(scene->door,SV_DoorRenderZ(fraction),true,doorCache);
 if(profile&&rebuilt)printf("SOLID cache update frame %.3f ms\n",(SDL_GetTicksNS()-started)/1e6);
 if(profile){static double total=0;static int frames=0;total+=(SDL_GetTicksNS()-started)/1e6;if(++frames==120){printf("SOLID mesh submission %.3f ms/frame\n",total/frames);frames=0;total=0;}}
 return true;
}
