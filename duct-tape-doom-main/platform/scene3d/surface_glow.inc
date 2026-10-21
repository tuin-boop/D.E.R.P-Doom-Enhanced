// Short-lived emissive fracture masks drawn on the current concrete mesh.
// Transient effects on surviving geometry; no actor or mesh-cache invalidation.
std::vector<Vertex> rubbleEmissionVertices;
struct SurfaceGlow {
    lab::Vec point,normal;std::array<float,3> color;
    float born,life,radius,anchor;int sector,plane;int weapon=0;
};
static std::vector<SurfaceGlow> surfaceGlows;
static unsigned surfaceGlowLevel=~0u;
static void resetSurfaceGlows(){
    if(surfaceGlowLevel!=r_levelserial){surfaceGlows.clear();surfaceGlowLevel=r_levelserial;}
}
static void addSurfaceGlow(int type,float x,float y,float z,float nx,float ny,float nz,std::array<float,3> color){
    resetSurfaceGlows();const auto* world=SV_MapWorld();if(!world)return;
    int sector=-1,plane=0;float anchor=0;
    world->solidOwner({x-nx*.5f,y-ny*.5f,z-nz*.5f},sector,plane,anchor);
    float radius=type==MT_BFG?52: type==MT_PLASMA||type==MT_ARACHPLAZ?24:36;
    float life=type==MT_BFG?4.5f:3.5f;
    if(type==MT_PLASMA||type==MT_ARACHPLAZ)color={.025f,.40f,1.f};
    SurfaceGlow glow{{x,y,z},{nx,ny,nz},color,gameSeconds(),life,radius,anchor,sector,plane,type};
    // Repeated fire refreshes a local patch instead of stacking brightness.
    for(auto &old:surfaceGlows)if(old.sector==sector&&old.plane==plane&&old.color==color&&old.weapon==type&&lab::length(old.point-glow.point)<radius*.45f){old=glow;return;}
    if(surfaceGlows.size()>=24)surfaceGlows.erase(surfaceGlows.begin());
    surfaceGlows.push_back(glow);
}
static int surfaceGlowImage(){
    const int lump=100000101,key=-1-lump;
    if(!images.count(key)){const byte white[]={0,255};images.emplace(key,upload(1,1,white));}
    return lump;
}
// Adjacent faces identify real concave folds. Coplanar triangulation edges
// and convex silhouettes emit nothing; no projected crack texture is used.
struct GlowFacet {lab::Vec p[3],normal;float fold[3]={};};
static std::vector<GlowFacet> glowFacets(const std::vector<Vertex>& source,size_t first,size_t count){
    std::vector<GlowFacet> faces;faces.reserve(count/3);
    using Point=std::array<int,3>;using Edge=std::array<Point,2>;
    std::map<Edge,std::pair<size_t,int>> edges;
    auto key=[](lab::Vec p){return Point{int(std::lround(p.x*256)),int(std::lround(p.y*256)),int(std::lround(p.z*256))};};
    for(size_t i=first;i+2<first+count;i+=3){
        GlowFacet f{};for(int k=0;k<3;++k)f.p[k]={source[i+k].x,source[i+k].y,source[i+k].z};
        f.normal=lab::normalized(lab::cross(f.p[1]-f.p[0],f.p[2]-f.p[0]));
        size_t index=faces.size();faces.push_back(f);
        for(int k=0;k<3;++k){Point a=key(f.p[k]),b=key(f.p[(k+1)%3]);if(b<a)std::swap(a,b);
            auto result=edges.emplace(Edge{a,b},std::make_pair(index,k));if(result.second)continue;
            auto &other=faces[result.first->second.first];int e=result.first->second.second;
            float turn=1-lab::dot(f.normal,other.normal);
            float concave=lab::dot(other.normal,f.p[(k+2)%3]-other.p[e]);
            if(concave>.025f&&turn>.06f){
                float heat=std::clamp((turn-.06f)*1.5f,0.f,1.f);
                faces[index].fold[k]=heat;other.fold[e]=heat;
            }
        }
    }
    return faces;
}
static lab::Vec surfaceGlowCenter(const SurfaceGlow& g){
    auto center=g.point;
    if(g.sector>=0&&g.sector<numsectors)center.z+=units(g.plane?sectors[g.sector].ceilingheight:sectors[g.sector].floorheight)-g.anchor;
    return center;
}
static void drawSurfaceGlows(){
    rubbleEmissionVertices.clear();
    auto oldScorch=effectBatches.find(100000101);
    if(oldScorch!=effectBatches.end())oldScorch->second.clear();
    resetSurfaceGlows();float now=gameSeconds();
    surfaceGlows.erase(std::remove_if(surfaceGlows.begin(),surfaceGlows.end(),[&](const auto& g){return now<g.born||now-g.born>=g.life;}),surfaceGlows.end());
    if(surfaceGlows.empty())return;
    int rock=W_CheckNumForName((char*)"DIGROCK");int material=rock>=0?-1-rock:R_TextureNumForName((char*)"GRAY1");
    auto batch=batches.find(material);if(batch==batches.end())return;
    auto sections=vertexSections.find(&batch->second);if(sections==vertexSections.end())return;
    auto& out=effectAddBatches[surfaceGlowImage()];const auto& source=batch->second;
    std::array<bool,24> illuminated={};
    std::array<int,24> arcs={};
    // Cached tile bounds keep cost local even after extensive destruction.
    struct Bounds {unsigned revision=~0u;lab::Vec lo,hi;std::vector<GlowFacet> faces;};
    static std::map<uint64_t,Bounds> bounds;static unsigned level=~0u;
    if(level!=r_levelserial){bounds.clear();level=r_levelserial;}
    for(const auto& section:sections->second){
        if(!section.count)continue;auto& box=bounds[section.id];
        if(box.revision!=section.revision){
            box.revision=section.revision;box.faces.clear();box.lo={1e9f,1e9f,1e9f};box.hi={-1e9f,-1e9f,-1e9f};
            for(size_t i=section.first;i<section.first+section.count;++i){const auto& v=source[i];
                box.lo={std::min(box.lo.x,v.x),std::min(box.lo.y,v.y),std::min(box.lo.z,v.z)};
                box.hi={std::max(box.hi.x,v.x),std::max(box.hi.y,v.y),std::max(box.hi.z,v.z)};
            }
        }
        for(const auto& g:surfaceGlows){
            lab::Vec center=surfaceGlowCenter(g);
            size_t patch=&g-surfaceGlows.data();
            const bool plasma=g.color[2]>.9f&&g.color[1]<.8f;
            const bool bfg=g.weapon==MT_BFG;
            const bool green=bfg||g.weapon==MT_BRUISERSHOT;
            const bool thermal=g.weapon==MT_ROCKET||g.weapon==MT_TRACER||
                g.weapon==MT_TROOPSHOT||g.weapon==MT_HEADSHOT||g.weapon==MT_FATSHOT;
            const bool fieldEffect=plasma||green||thermal;
            float age=now-g.born;
            float r=g.radius;
            if(center.x+r<box.lo.x||center.x-r>box.hi.x||center.y+r<box.lo.y||center.y-r>box.hi.y||center.z+r<box.lo.z||center.z-r>box.hi.z)continue;
            if(box.faces.empty())box.faces=glowFacets(source,section.first,section.count);
            float fade=std::clamp((g.life-(now-g.born))/(g.life-.5f),0.f,1.f);
            float pulse=.88f+.12f*std::sin((now-g.born)*16);
            for(const auto &face:box.faces){
                if(out.size()>35760)break;
                float fold=std::max({face.fold[0],face.fold[1],face.fold[2]});if(fold<=0&&!fieldEffect)continue;
                float weight[3];bool any=false;
                for(int k=0;k<3;++k){weight[k]=std::max(0.f,1-lab::length(face.p[k]-center)/r);any|=weight[k]>0;}
                if(!any)continue;
                auto vertex=[&](lab::Vec p,float heat,float core=0.f){
                    Vertex a{};auto q=p+face.normal*.025f;
                    a.x=q.x;a.y=q.y;a.z=q.z;a.u=a.v=.5f;
                    float reach=std::max(0.f,1-lab::length(p-center)/r);
                    a.light=std::min(.85f,heat*std::sqrt(reach)*fade*pulse);
                    // Only the hottest narrow cores approach white; cooling keeps the
                    // weapon color, and orange embers remain warm rather than white.
                    float whitening=core*fade*(g.color[2]>.5f||g.color[1]>.8f?.55f:.22f);
                    a.red=g.color[0]+(1-g.color[0])*whitening;
                    a.green=g.color[1]+(1-g.color[1])*whitening;
                    a.blue=g.color[2]+(1-g.color[2])*whitening;return a;
                };
                if(fieldEffect){
                    illuminated[patch]=true;
                    for(int k=0;k<3;++k){
                        auto v=vertex(face.p[k],1.f);
                        auto local=face.p[k]-center;
                        v.mode=2048u|(thermal?1024u:0u);v.u=local.x;v.v=local.y;v.sunU=local.z;v.sunV=r;
                        v.light=fade*pulse;v.statics=(unsigned)(fade*65535.f);
                        if(green){v.red=.12f;v.green=1.35f;v.blue=.045f;}
                        else if(thermal){v.red=3.2f;v.green=.24f+.88f*std::sqrt(fade);v.blue=.008f;}
                        else {v.red=.035f;v.green=.48f;v.blue=1.35f;}
                        out.push_back(v);
                        {
                            auto scorch=v;scorch.mode|=256u;scorch.light=std::sqrt(fade);
                            effectBatches[surfaceGlowImage()].push_back(scorch);
                        }
                    }
                    continue;
                }
                // Irregular luminous pockets lie inside real folded facets. Their
                // dark borders retain depth instead of outlining the triangulation.
                auto centroid=(face.p[0]+face.p[1]+face.p[2])*(1.f/3.f);
                auto local=centroid-center;
                float pocketSeed=std::sin(local.x*.71f+local.y*1.13f+local.z*2.31f)*173.73f;
                float pocket=pocketSeed-std::floor(pocketSeed);
                float reach=std::max(0.f,1-lab::length(centroid-center)/r);
                // Low-frequency variation groups nearby hot facets into pockets,
                // rather than scattering identical highlights over every triangle.
                float cluster=.5f+.25f*std::sin(local.x*.19f+local.z*.23f)
                    +.25f*std::sin(local.y*.21f-local.z*.17f+1.7f);
                float pocketLife=std::min(g.life,.85f+fold*1.3f+pocket*1.35f);
                float cooling=std::clamp((pocketLife-age)/.8f,0.f,1.f);
                if(reach>0 && pocket>(plasma?.38f:.30f) && (!plasma||cluster>.40f) && cooling>0){
                    illuminated[patch]=true;
                    int valley=0;
                    for(int e=1;e<3;++e)if(face.fold[e]>face.fold[valley])valley=e;
                    auto seam=(face.p[valley]+face.p[(valley+1)%3])*.5f;
                    auto hot=centroid+(seam-centroid)*.60f;
                    float flicker=.86f+.14f*std::sin(age*(9+pocket*7)+pocket*29);
                    float energy=(plasma?2.4f:.85f)*std::sqrt(fold)*(.65f+.35f*pocket)*cooling*flicker;
                    // A saturated, irregular outer pocket surrounds a small hot
                    // core. Both layers stay inside the original concrete facet.
                    for(int layer=0;layer<(plasma?2:1);++layer){
                        float extent=layer?.28f:.85f;
                        lab::Vec rim[6];
                        for(int k=0;k<6;++k){
                            int edge=k/2;
                            float t=(k&1)?.79f:.17f;
                            auto target=face.p[edge]+(face.p[(edge+1)%3]-face.p[edge])*t;
                            auto delta=target-hot;
                            float irregular=.82f+.18f*std::sin(pocket*21+k*2.7f);
                            float scale=extent*irregular;
                            if(plasma)scale=std::min(scale,(layer?1.15f:4.5f)/std::max(.01f,lab::length(delta)));
                            rim[k]=hot+delta*scale;
                        }
                        for(int k=0;k<6;++k){
                            auto coreVertex=vertex(hot,energy*(layer?1.f:.65f),layer?1.5f:0.f);
                            if(fieldEffect){coreVertex.red*=2.4f;coreVertex.green*=2.4f;coreVertex.blue*=2.4f;}
                            out.push_back(coreVertex);
                            out.push_back(vertex(rim[k],0));
                            out.push_back(vertex(rim[(k+1)%6],0));
                        }
                    }

                    // Brief, sparse electrical filaments stay inside the face,
                    // so they cannot bridge an intact wall or a newly opened hole.
                    int beat=int(age*14);
                    if(plasma&&age<1.15f&&arcs[patch]<2&&pocket>.68f&&((beat+int(pocket*31))%4)==0){
                        ++arcs[patch];
                        auto begin=hot+(face.p[1]-hot)*.55f;
                        auto end=hot+(face.p[2]-hot)*.55f;
                        auto previous=begin;
                        auto side=lab::normalized(lab::cross(face.normal,end-begin))*.10f;
                        for(int k=1;k<=4;++k){
                            float t=k*.25f;
                            auto next=begin+(end-begin)*t;
                            if(k<4)next=next+(hot-next)*(.18f+.14f*std::sin(float(beat*7+k*3)));
                            out.push_back(vertex(previous-side,2.f,1));out.push_back(vertex(next-side,2.f,1));out.push_back(vertex(next+side,2.f,1));
                            out.push_back(vertex(previous-side,2.f,1));out.push_back(vertex(next+side,2.f,1));out.push_back(vertex(previous+side,2.f,1));
                            previous=next;
                        }
                    }
                }
                // Subtle light inside folded faces, with stronger narrow heat
                // at their actual shared valley edges. The stone stays visible.
                for(int k=0;k<3;++k)out.push_back(vertex(face.p[k],std::sqrt(fold)*(plasma?.018f:.07f)));
                for(int e=0;e<3;++e){
                    if(face.fold[e]<=0)continue;
                    auto a=face.p[e],b=face.p[(e+1)%3],c=face.p[(e+2)%3];
                    // Leave cold gaps at junctions instead of outlining every
                    // triangle. The shape stays on the two actual valley faces.
                    auto mid=(a+b)*.5f-center;
                    float seed=std::sin(mid.x*1.37f+mid.y*2.19f+mid.z*.73f)*437.31f;
                    float variation=seed-std::floor(seed);
                    if(variation<.34f&&face.fold[e]<.65f)continue;
                    float start=.10f+variation*.13f,end=.79f+variation*.12f;
                    float peak=.38f+variation*.24f;
                    float positions[4]={start,peak-.12f,peak+.12f,end};
                    float heat=std::sqrt(face.fold[e])*(1.1f+variation*.6f);
                    float shimmer=.9f+.1f*std::sin((now-g.born)*11+variation*12);
                    float altitude=lab::length(lab::cross(b-a,c-a))/std::max(.01f,lab::length(b-a));
                    for(int layer=0;layer<2;++layer){
                        float width=layer?.45f:2.2f+variation*.9f;
                        float t=std::min(.32f,width/std::max(.01f,altitude));
                        float strength=heat*shimmer*(layer?.85f:.55f)*(plasma?.45f:1.f);
                        for(int segment=0;segment<3;++segment){
                            auto p=a+(b-a)*positions[segment],q=a+(b-a)*positions[segment+1];
                            // Width and light both taper into the stone.
                            auto innerP=p+(c-p)*(segment?t:t*.15f),innerQ=q+(c-q)*(segment==2?t*.15f:t);
                            float hp=segment?strength:0,hq=segment==2?0:strength;
                            out.push_back(vertex(p,hp,float(layer)));out.push_back(vertex(q,hq,float(layer)));out.push_back(vertex(innerQ,0));
                            out.push_back(vertex(p,hp,float(layer)));out.push_back(vertex(innerQ,0));out.push_back(vertex(innerP,0));
                        }
                    }
                }
            }
        }
    }
    // At most two recent plasma patches cast a short, occluded light spill.
    // Constant radius/position allows the existing blocker cache to be reused.
    int spillCount=0;
    for(size_t i=surfaceGlows.size();i-->0&&spillCount<2;){
        const auto& g=surfaceGlows[i];float age=now-g.born;
        if(!illuminated[i]||g.color[2]<.9f||g.color[1]>=.8f||age>=1.15f)continue;
        auto p=surfaceGlowCenter(g)+g.normal*4.f;
        auto previousKind=lightKind;lightKind=I_RENDER3D_LIGHT_PROJECTILE;
        appendLight(p.x,p.y,p.z,72,.38f*(1-age/1.15f),g.color,nullptr,.65f);
        lightKind=previousKind;++spillCount;
    }
    // Keep high radiance out of the LDR surface pass. Only visible heated
    // concrete enters bloom; ordinary wall textures and the HUD do not.
    rubbleEmissionVertices=out;
    for(auto& vertex:rubbleEmissionVertices){
        float radiance=(vertex.mode&1024u)?2.5f:4.5f;
        vertex.red*=radiance;vertex.green*=radiance;vertex.blue*=radiance;
    }

}
