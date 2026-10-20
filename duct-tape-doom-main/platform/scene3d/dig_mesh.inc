// Lab-only surface reconstruction. Collision/save data stays in columns;
// marching tetrahedra replaces their stair-step skin with fractured facets.
extern "C" int P_DigEnabled(void);
static bool digMeshEnabled(){return P_DigEnabled()&&numsectors==64*40;}
static void buildDigMesh(){
    if(!digMeshEnabled())return;
    static uint64_t previous=0;
    static std::map<int,std::vector<Vertex>> cached;
    uint64_t key=r_levelserial;
    for(int i=0;i<numsectors;++i){key=(key^(unsigned)sectors[i].floorheight)*1099511628211ULL;key=(key^(unsigned)sectors[i].ceilingheight)*1099511628211ULL;}
    if(key!=previous){
        previous=key;cached.clear();
        int concrete=R_TextureNumForName((char*)"GRAY1"),skin=R_TextureNumForName((char*)"STARTAN3");
        wallImage(concrete);wallImage(skin);
        using V=std::array<float,3>;
        auto sub=[](V a,V b){return V{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
        auto cross=[](V a,V b){return V{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        auto emit=[&](V a,V b,V c){
            V mid={(a[0]+b[0]+c[0])/3,(a[1]+b[1]+c[1])/3,(a[2]+b[2]+c[2])/3};
            V normal=cross(sub(b,a),sub(c,a));float length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
            if(length<.001f)return;for(float &n:normal)n/=length;
            int x=std::clamp(int(mid[0]/16),0,63),y=std::clamp(int(mid[1]/16),0,39);
            sector_t *sector=&sectors[y*64+x];
            // Existing room floors/ceilings continue up to the original rim.
            if(sector->tag!=19001&&std::abs(normal[2])>.95f&&(mid[2]<1||mid[2]>127))return;
            bool faceX=std::abs(normal[0])>=std::abs(normal[1]);
            float boundaryX=std::min({std::abs(mid[0]-64),std::abs(mid[0]-320),std::abs(mid[0]-704),std::abs(mid[0]-960)});
            float boundaryY=std::min(std::abs(mid[1]-96),std::abs(mid[1]-544));
            bool damaged=false;
            for(int yy=std::max(0,y-1);yy<=std::min(39,y+1);++yy)for(int xx=std::max(0,x-1);xx<=std::min(63,x+1);++xx){
                const auto &s=sectors[yy*64+xx];damaged|=s.tag==19001&&s.ceilingheight>s.floorheight;
            }
            bool outer=std::min(boundaryX,boundaryY)<8.1f&&(!damaged||(std::abs(normal[2])<.3f&&std::min(boundaryX,boundaryY)<.5f));
            auto &out=cached[outer?skin:concrete];
            float light=lighting(sector);
            // Stable facet shading makes broken planes readable without
            // relying on stale pre-excavation static-light bakes.
            if(!outer)light*=.78f+.22f*std::abs(normal[2])+.10f*std::abs(normal[0]);
            if(!outer)light*=.68f+.32f*std::min(1.f,std::min(boundaryX,boundaryY)/20.f);
            for(V p:{a,b,c}){
                float u,v;
                if(outer){u=boundaryX<boundaryY?p[1]:p[0];v=-p[2];}
                else if(std::abs(normal[2])>.6f){u=p[0];v=-p[1];}
                else {u=faceX?p[1]:p[0];v=-p[2];}
                out.push_back({p[0],p[1],p[2],u,v,light,sectorMode(sector)});
            }
        };
        const int corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
        const int tetra[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
        for(int y=0;y<39;++y)for(int x=0;x<63;++x){
            bool relevant=false,open=false;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto &s=sectors[(y+dy)*64+x+dx];relevant|=s.tag==19001;open|=s.ceilingheight>s.floorheight;}
            if(!relevant||!open)continue;
            for(int z=-1;z<17;++z){
                V points[8];float density[8];
                for(int i=0;i<8;++i){
                    int xx=x+corner[i][0],yy=y+corner[i][1],zz=z+corner[i][2];
                    points[i]={8.f+xx*16,8.f+yy*16,zz*8.f};
                    const auto &s=sectors[yy*64+xx];
                    float d=s.ceilingheight<=s.floorheight?8.f:std::clamp(std::max(units(s.floorheight)-zz*8.f,zz*8.f-units(s.ceilingheight)),-8.f,8.f);
                    // Intact rooms keep their original floor/ceiling meshes.
                    // Extending their empty sample vertically prevents an
                    // artificial bevel along every undamaged wall foot/top.
                    if(s.tag!=19001&&s.floorheight<s.ceilingheight)d=-8.f;
                    // Do not perturb the saturated solid/empty field: that
                    // warps the intact wall above a crater into stray facets.
                    if(s.tag==19001&&s.floorheight<s.ceilingheight&&d>-7.99f&&d<7.99f){
                        unsigned h=(xx*73856093u)^(yy*19349663u)^(zz*83492791u);h^=h>>13;
                        d+=(int(h%9)-4)*.45f;
                    }
                    density[i]=d==0?.001f:d;
                }
                auto edge=[&](int a,int b){float t=density[a]/(density[a]-density[b]);V p;for(int j=0;j<3;++j)p[j]=points[a][j]+(points[b][j]-points[a][j])*t;return p;};
                for(const auto &t:tetra){
                    int in[4],out[4],ni=0,no=0;
                    for(int i:t)if(density[i]<0)in[ni++]=i;else out[no++]=i;
                    if(ni==1)emit(edge(in[0],out[0]),edge(in[0],out[1]),edge(in[0],out[2]));
                    else if(ni==3)emit(edge(out[0],in[0]),edge(out[0],in[2]),edge(out[0],in[1]));
                    else if(ni==2){V a=edge(in[0],out[0]),b=edge(in[0],out[1]),c=edge(in[1],out[0]),d=edge(in[1],out[1]);emit(a,b,c);emit(b,d,c);}
                }
            }
        }
    }
    for(const auto &part:cached){auto &out=batches[part.first];out.insert(out.end(),part.second.begin(),part.second.end());}
}
