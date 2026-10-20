// Shared lattice tessellation is cached separately for each affected BSP leaf.
static bool addExcavatedFlat(int leaf,int ceiling,int original,float z,float light,unsigned mode){
    if(!P_GlobalDigEnabled())return false;
    int sector=int(subsectors[leaf].sector-sectors);
    if(!P_GlobalDigSolidFlat(sector,ceiling))return false;
    struct Cache{unsigned revision=0,query=0,version=0;int material=0,deep=-1;float z=0,bounds[4]={};unsigned mode=0;std::map<int,std::vector<Vertex>> vertices;};
    static std::map<int,Cache> cache;static int serial=-1;
    if(serial!=r_levelserial){cache.clear();serial=r_levelserial;}
    Cache &c=cache[leaf*2+ceiling];
    if(c.query!=P_GlobalDigRevision()){
        float area[4]={1e9f,-1e9f,1e9f,-1e9f};
        for(Point p:floors[leaf]){area[0]=std::min(area[0],p.x);area[1]=std::max(area[1],p.x);area[2]=std::min(area[2],p.y);area[3]=std::max(area[3],p.y);}
        c.version=P_GlobalDigFlatPatch(sector,ceiling,area,c.bounds);c.query=P_GlobalDigRevision();
    }
    unsigned version=c.version;if(!version)return false;const float *bounds=c.bounds;
    int concrete=W_CheckNumForName((char*)"DIGROCK");
    if(concrete<0)return false;lumpImage(concrete,true);concrete=-1-concrete;
    if(c.revision!=version||c.z!=z||c.deep!=p_deeperdig){
        uint64_t began=SDL_GetTicksNS();
        c.revision=version;c.z=z;c.deep=p_deeperdig;c.material=original;c.mode=mode;c.vertices.clear();
        // Clip a shared world lattice to each BSP polygon. Independent adaptive
        // subdivisions made T junctions whose heights disagreed at leaf edges.
        auto clipAxis=[](std::vector<Point> poly,int axis,float edge,bool greater){
            std::vector<Point> out;if(poly.empty())return out;
            Point prev=poly.back();float pv=axis?prev.y:prev.x;bool pin=greater?pv>=edge:pv<=edge;
            for(Point cur:poly){float cv=axis?cur.y:cur.x;bool in=greater?cv>=edge:cv<=edge;
                if(in!=pin){float t=(edge-pv)/(cv-pv);Point q={prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y)};if(axis)q.y=edge;else q.x=edge;out.push_back(q);}
                if(in)out.push_back(cur);prev=cur;pv=cv;pin=in;
            }return out;
        };
        auto emit=[&](const std::vector<Point>& poly,bool damaged){
            for(size_t n=1;n+1<poly.size();++n){Point points[3]={poly[0],poly[n],poly[n+1]};float depths[3]={};
                if(std::abs((points[1].x-points[0].x)*(points[2].y-points[0].y)-(points[1].y-points[0].y)*(points[2].x-points[0].x))<1e-6f)continue;
                for(int i=0;i<3;++i)if(damaged)depths[i]=P_GlobalDigFlatDepth(sector,ceiling,points[i].x,points[i].y);
                bool rock=std::max({depths[0],depths[1],depths[2]})>.02f;auto &out=c.vertices[rock?concrete:original];
                size_t first=out.size();
                for(int i=0;i<3;++i){Point p=points[i];float depth=depths[i],shade=rock?.65f+.2f*std::min(1.f,depth/2):1;
                    out.push_back({p.x,p.y,z+(ceiling?depth:-depth),p.x,-p.y,shade,rock?(sectorMode(&sectors[sector])|MODE_EXCAVATED_FLAT):mode});}
                excavationNormal(out[first],out[first+1],out[first+2]);
            }
        };
        std::vector<Point> inner=floors[leaf];
        float edges[4]={std::floor(bounds[0]/4)*4,std::ceil(bounds[1]/4)*4,std::floor(bounds[2]/4)*4,std::ceil(bounds[3]/4)*4};
        for(int k=0;k<4;++k){int axis=k/2;bool greater=(k%2)==0;emit(clipAxis(inner,axis,edges[k],!greater),false);inner=clipAxis(inner,axis,edges[k],greater);}
        if(!inner.empty()){
            float lx=inner[0].x,hx=lx,ly=inner[0].y,hy=ly;
            for(Point p:inner){lx=std::min(lx,p.x);hx=std::max(hx,p.x);ly=std::min(ly,p.y);hy=std::max(hy,p.y);}
            // Subdivide only rectangles overlapping damage, not the entire
            // bounding box between distant holes in the same BSP leaf.
            auto tessellate=[&](auto &&self,const std::vector<Point>& poly,float x,float y,float right,float top)->void{
                if(poly.size()<3)return;
                float area[4]={x,right,y,top},hit[4];
                if(!P_GlobalDigFlatPatch(sector,ceiling,area,hit)){emit(poly,false);return;}
                if(right-x>4||top-y>4){
                    bool horizontal=right-x>=top-y;int axis=horizontal?0:1;
                    float low=horizontal?x:y,high=horizontal?right:top;
                    float mid=low+std::floor((high-low)/8)*4;if(mid<=low)mid=low+4;
                    auto a=clipAxis(poly,axis,mid,false),b=clipAxis(poly,axis,mid,true);
                    self(self,a,x,y,horizontal?mid:right,horizontal?top:mid);
                    self(self,b,horizontal?mid:x,horizontal?y:mid,right,top);return;
                }
                // Match the collision lattice diagonal exactly.
                for(bool upper:{false,true}){std::vector<Point> part;Point prev=poly.back();float pd=(prev.y-y)-(prev.x-x);bool pin=upper?pd>=0:pd<=0;
                    for(Point cur:poly){float cd=(cur.y-y)-(cur.x-x);bool in=upper?cd>=0:cd<=0;if(in!=pin){float t=pd/(pd-cd);part.push_back({prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y)});}if(in)part.push_back(cur);prev=cur;pd=cd;pin=in;}emit(part,true);}
            };
            tessellate(tessellate,inner,std::floor(lx/4)*4,std::floor(ly/4)*4,std::ceil(hx/4)*4,std::ceil(hy/4)*4);
        }
        if(M_CheckParm((char*)"-profiledig")){size_t n=0;for(auto &part:c.vertices)n+=part.second.size();fprintf(stderr,"Flat mesh %d/%d: %zu vertices %.3f ms\n",leaf,ceiling,n,(SDL_GetTicksNS()-began)/1e6);}
    }
    // Animated flat frames and mirror flags do not change crater geometry.
    for(const auto &part:c.vertices){bool skin=part.first==c.material;auto &out=batches[skin?original:part.first];
        for(Vertex v:part.second){if(skin)v.mode=mode|(v.mode&MODE_FACET_NORMAL);v.light*=light;out.push_back(v);}}
    return true;
}
