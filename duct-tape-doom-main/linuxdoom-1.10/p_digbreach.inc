/* Optional portals through finite solid wall slabs. Topology is immutable:
 * both boundary faces share one aperture and the existing crater is its rim.
 * Only a nearest, opposing room boundary may be an exit (never the void). */
typedef struct {int source,side,exit,exitSide,solidSector,solidPlane;float depth,offset,lo,hi;} breach_t;
typedef struct {int link,next;} breach_ref_t;
static breach_t breaches[MAX_CUTS];
static breach_ref_t breachRefs[MAX_CUTS*2];
static int breachCount,breachSeen,breachRefCount,*breachHeads;
static sector_t *faceRoom(int line,int side){return side?lines[line].backsector:lines[line].frontsector;}
static sector_t *breachRoom(const breach_t *b,int exit){return faceRoom(exit?b->exit:b->source,exit?b->exitSide:b->side);}
static void resetBreaches(void){free(breachHeads);breachHeads=NULL;breachCount=breachSeen=breachRefCount=0;}
static void indexBreaches(void){
    int mode=P_GlobalDigThroughEnabled();static int previousMode=-1;
    if(!mode)return;
    if(mode!=previousMode){resetBreaches();previousMode=mode;}
    if(!breachHeads){breachHeads=malloc((size_t)numlines*2*sizeof(int));if(!breachHeads)I_Error("Breach index allocation failed");for(int i=0;i<numlines*2;++i)breachHeads[i]=-1;}
    for(;breachSeen<count;++breachSeen){cut_t *c=&cuts[breachSeen];
        sector_t *slab=faceRoom(c->line,1-c->side);
        if(slab?!p_breachdoors:!p_throughwalls)continue;
        // Ceiling-backed doors and floor-backed lifts can both be breached. The opposite face must
        // border this same slab, so an unrelated room is never skipped.
        if(slab&&cutAnchors[breachSeen].sector<0)continue;
        double ax,ay,tx,ty,len=frame(c->line,c->side,&ax,&ay,&tx,&ty);
        if(!len)continue;
        double sampleU=fmax(.001,fmin(len-.001,c->u));
        double x=ax+sampleU*tx,y=ay+sampleU*ty,nx=-ty,ny=tx,best=P_GlobalDigMaxDepth()+.001;int target=-1;
        for(int i=0;i<numlines;++i){if(i==c->line)continue;
            double bx=unit(lines[i].v1->x),by=unit(lines[i].v1->y),dx=unit(lines[i].dx),dy=unit(lines[i].dy);
            double det=nx*dy-ny*dx;if(fabs(det)<1e-8)continue;
            double t=((bx-x)*dy-(by-y)*dx)/det,v=((bx-x)*ny-(by-y)*nx)/det;
            if(t>.01&&v>=0&&v<=1&&t<best){best=t;target=i;}
        }
        if(target<0)continue;
        int exitSide=0;
        if(slab){
            if(lines[target].frontsector==slab)exitSide=1;
            else if(lines[target].backsector==slab)exitSide=0;
            else continue;
            if(lines[target].sidenum[exitSide]<0||!faceRoom(target,exitSide))continue;
        }else if(lines[target].backsector||!lines[target].frontsector)continue;
        double bx,by,ux,uy,other=frame(target,exitSide,&bx,&by,&ux,&uy);
        if(!other||tx*ux+ty*uy>-.99999)continue;
        float offset=(float)((bx-ax)*tx+(by-ay)*ty);
        float lo=fmaxf(0,offset-(float)other),hi=fminf((float)len,offset);
        if(hi-lo<1)continue;
        int exists=0;for(int j=0;j<breachCount;++j)if(breaches[j].source==c->line&&breaches[j].side==c->side&&breaches[j].exit==target){exists=1;break;}
        if(exists)continue;
        int n=breachCount++;breaches[n]=(breach_t){c->line,c->side,target,exitSide,slab?(int)(slab-sectors):-1,cutAnchors[breachSeen].plane,(float)best,offset,lo,hi};
        int keys[2]={c->line*2+c->side,target*2+exitSide};
        for(int k=0;k<2;++k){breachRefs[breachRefCount]=(breach_ref_t){n,breachHeads[keys[k]]};breachHeads[keys[k]]=breachRefCount++;}
    }
}
static int firstBreach(int line,int side){if(!P_GlobalDigThroughEnabled())return -1;indexBreaches();return breachHeads[line*2+side];}
int P_GlobalDigBreachFace(int line,int side){return firstBreach(line,side)>=0;}
static float breachU(const breach_t *b,int line,float u){return line==b->source?u:b->offset-u;}
static float breachJoinMargin(const breach_t *b,int end,float z){
    int key=joinedFace(b->source,b->side,end);if(key<0)return -10000;
    double ax,ay,tx,ty,len=frame(b->source,b->side,&ax,&ay,&tx,&ty);
    double bx,by,ux,uy,other=frame(key/2,key%2,&bx,&by,&ux,&uy);
    if(tx*ux+ty*uy<.99999)return -10000; // A turn remains a real volume boundary.
    float at=end?0:(float)other,result=-10000;
    for(int ref=breachHeads[key];ref>=0;ref=breachRefs[ref].next){
        breach_t *n=&breaches[breachRefs[ref].link];
        if(n->source!=key/2||n->side!=key%2||fabsf(n->depth-b->depth)>.01f||
           n->solidSector!=b->solidSector||n->solidPlane!=b->solidPlane||
           breachRoom(n,1)!=breachRoom(b,1)||at<n->lo-.001||at>n->hi+.001)continue;
        result=fmaxf(result,sampleDepth(n->source,n->side,at,z,1)-n->depth);
    }return result;
}
// Closed door sectors can become the rooms at either end of a side breach.
// Derive their usable height from actual adjoining open rooms, never the void.
static void breachRoomHeights(const sector_t *room,float *floor,float *ceiling){
    *floor=(float)unit(room->floorheight);*ceiling=(float)unit(room->ceilingheight);
    if(*ceiling>*floor)return;
    float low=-1e9f,high=1e9f;int found=0;
    for(int i=0;i<room->linecount;++i){line_t *l=room->lines[i];sector_t *other=l->frontsector==room?l->backsector:l->frontsector;
        if(!other||other==room||other->ceilingheight<=other->floorheight)continue;
        low=fmaxf(low,(float)unit(other->floorheight));high=fminf(high,(float)unit(other->ceilingheight));found=1;
    }
    if(found&&high>low){*floor=low;*ceiling=high;}
}
static float breachMargin(const breach_t *b,float u,float z){
    sector_t *a=breachRoom(b,0),*e=breachRoom(b,1);
    float af,ac,ef,ec;breachRoomHeights(a,&af,&ac);breachRoomHeights(e,&ef,&ec);
    float margin=sampleDepth(b->source,b->side,u,z,1)-b->depth;
    // Small feet left at the bottom of a blasted door can be surrounded by
    // open passage on three sides, but cannot pass the eight-neighbour test
    // because the supporting floor is deliberately solid. Trim only short,
    // narrow remnants with a breached opening above and to either side.
    float base=fmaxf(af,ef);
    if(b->solidSector>=0&&z>=base&&z<base+24&&margin<=0){
        float above=sampleDepth(b->source,b->side,u,base+28,1)-b->depth;
        float leftFoot=sampleDepth(b->source,b->side,u-16,base+16,1)-b->depth;
        float rightFoot=sampleDepth(b->source,b->side,u+16,base+16,1)-b->depth;
        float clear=fminf(above,fminf(leftFoot,rightFoot));
        if(clear>0)margin=fmaxf(margin,fminf(clear,base+24-z));
    }
    // The opening below a rising door joins seamlessly to its moving hole.
    if(b->solidSector>=0)margin=fmaxf(margin,b->solidPlane?(float)unit(sectors[b->solidSector].ceilingheight)-z:z-(float)unit(sectors[b->solidSector].floorheight));
    // Doom permits a body to exactly fit a corridor (32 units wide).
    float left=u-b->lo+.05f,right=b->hi-u+.05f;
    double ax,ay,tx,ty,len=frame(b->source,b->side,&ax,&ay,&tx,&ty);
    if(left<1&&b->lo<.001f&&u>=b->lo-.001f)left=fmaxf(left,breachJoinMargin(b,0,z));
    if(right<1&&b->hi>len-.001f&&u<=b->hi+.001f)right=fmaxf(right,breachJoinMargin(b,1,z));
    margin=fminf(margin,fminf(left,right));
    margin=fminf(margin,z-fmaxf(af,ef));
    margin=fminf(margin,fminf(ac,ec)-z);
    return margin;
}
float P_GlobalDigBreachMargin(int line,int side,float u,float z){
    // Continue body samples across split coplanar faces instead of inventing
    // an impassable post at the mapper's segmentation vertex.
    for(int step=0;step<16;++step){
        double ax,ay,tx,ty,len=frame(line,side,&ax,&ay,&tx,&ty);
        if(u>=0&&u<=len)break;
        int end=u>len,key=joinedFace(line,side,end);if(key<0)return -10000;
        double bx,by,ux,uy,other=frame(key/2,key%2,&bx,&by,&ux,&uy);
        if(tx*ux+ty*uy<.99999)return -10000;
        u=end?u-(float)len:u+(float)other;line=key/2;side=key%2;
    }
    float result=-10000;
    for(int r=firstBreach(line,side);r>=0;r=breachRefs[r].next){breach_t *b=&breaches[breachRefs[r].link];result=fmaxf(result,breachMargin(b,breachU(b,line,u),z));}
    return result;
}
float P_GlobalDigBreachLimit(int line,int side,float u,float z){
    for(int r=firstBreach(line,side);r>=0;r=breachRefs[r].next){breach_t *b=&breaches[breachRefs[r].link];
        if(b->source!=line||u<b->lo||u>b->hi)continue;
        sector_t *e=breachRoom(b,1);
        float floor,ceiling;breachRoomHeights(e,&floor,&ceiling);
        if(z>=floor&&z<=ceiling)return b->depth;
    }return 0;
}
static unsigned breachRevision(int line,int side){
    unsigned v=0;indexCuts();
    for(int r=firstBreach(line,side);r>=0;r=breachRefs[r].next){breach_t *b=&breaches[breachRefs[r].link];
        for(int i=heads[b->source*2+b->side];i>=0;i=nextCut[i])if(cutVersions[i]>v)v=cutVersions[i];
        for(int end=0;end<2;++end){int key=joinedFace(b->source,b->side,end);if(key<0)continue;
            for(int i=heads[key];i>=0;i=nextCut[i])if(cutVersions[i]>v)v=cutVersions[i];
        }
    }return v;
}
static void breachBounds(int line,int side,float *out,int *found){
    indexCuts();
    for(int r=firstBreach(line,side);r>=0;r=breachRefs[r].next){breach_t *b=&breaches[breachRefs[r].link];
        if(line==b->source)continue;
        for(int i=heads[b->source*2+b->side];i>=0;i=nextCut[i]){float u=b->offset-cuts[i].u,z=cutHeight(i);
            out[0]=fminf(out[0],u-32);out[1]=fmaxf(out[1],u+32);out[2]=fminf(out[2],z-32);out[3]=fmaxf(out[3],z+32);*found=1;
        }
    }
}
static int touchesBreach(int line,const breach_t *b){
    if(line==b->source||line==b->exit)return 0;
    line_t *l=&lines[line];int ids[2]={b->source,b->exit};
    for(int i=0;i<2;++i){line_t *o=&lines[ids[i]];
        if(l->v1==o->v1||l->v1==o->v2||l->v2==o->v1||l->v2==o->v2)return 1;
    }return 0;
}
float P_GlobalDigVoidMargin(int line,float x,float y,float z){
    if(!P_GlobalDigThroughEnabled())return -10000;indexBreaches();float result=-10000;
    for(int i=0;i<breachCount;++i){breach_t *b=&breaches[i];if(!touchesBreach(line,b))continue;
        double ax,ay,tx,ty;frame(b->source,b->side,&ax,&ay,&tx,&ty);
        float u=(float)((x-ax)*tx+(y-ay)*ty),d=(float)(-(x-ax)*ty+(y-ay)*tx);
        // Rendering removes only the interior of the excavated volume.
        // Collision's fit tolerance must not erase its enclosing faces:
        // an adjacent wall on u=lo/hi is the boundary of the hole, not air.
        // Keep a continuous signed distance outside the box as well, so
        // triangle intersections cannot interpolate against a sentinel.
        float box=fminf(fminf(u-b->lo,b->hi-u),fminf(d,b->depth-d));
        float margin=fminf(breachMargin(b,u,z),box)-.001f;result=fmaxf(result,margin);
    }return result;
}
static unsigned otherBreachRevision(int line){
    if(!P_GlobalDigThroughEnabled())return 0;indexBreaches();indexCuts();unsigned value=0;
    for(int i=0;i<breachCount;++i){breach_t *b=&breaches[i];if(!touchesBreach(line,b))continue;
        for(int c=heads[b->source*2+b->side];c>=0;c=nextCut[c])if(cutVersions[c]>value)value=cutVersions[c];
    }return value;
}
static int breachPointOpen(double x,double y,double z){
    if(!P_GlobalDigThroughEnabled())return 0;indexBreaches();
    for(int i=0;i<breachCount;++i){breach_t *b=&breaches[i];double ax,ay,tx,ty;frame(b->source,b->side,&ax,&ay,&tx,&ty);
        double u=(x-ax)*tx+(y-ay)*ty,d=-(x-ax)*ty+(y-ay)*tx;
        if(d<-.05||d>b->depth+.05||u<b->lo-.01||u>b->hi+.01)continue;
        if(breachMargin(b,(float)u,(float)z)>.02f)return 1;
    }return 0;
}
static double breachFitHeight(int line,int side,mobj_t *thing,fixed_t x,fixed_t y){
    if(firstBreach(line,side)<0)return -1e20;
    double ax,ay,tx,ty;frame(line,side,&ax,&ay,&tx,&ty);
    double u=(unit(x)-ax)*tx+(unit(y)-ay)*ty,radius=unit(thing->radius)*(fabs(tx)+fabs(ty));
    double bottom=unit(thing->z);
    // Ground actors retain Doom's normal 24-unit step when rooms differ in height.
    if(!(thing->flags&(MF_MISSILE|MF_FLOAT|MF_SKULLFLY))){
        for(int ref=firstBreach(line,side);ref>=0;ref=breachRefs[ref].next){breach_t *b=&breaches[breachRefs[ref].link];
            float sourceU=breachU(b,line,(float)u);if(sourceU<b->lo||sourceU>b->hi)continue;
            float af,ac,ef,ec;breachRoomHeights(breachRoom(b,0),&af,&ac);breachRoomHeights(breachRoom(b,1),&ef,&ec);
            double floor=fmax(af,ef);
            if(floor>bottom&&floor-bottom<=24)bottom=floor;
        }
    }
    // A fractured sill is a normal step, not a full-height invisible wall.
    int grounded=!(thing->flags&(MF_MISSILE|MF_FLOAT|MF_SKULLFLY))&&thing->z<=thing->floorz+FRACUNIT;
    double highest=grounded?unit(thing->z)+24:bottom;
    for(double base=bottom;base<=highest+.001;base+=1){
        int clear=1;
        for(double h=base+.1;h<base+unit(thing->height)&&clear;h+=2)
            for(double v=u-radius;;v=fmin(v+2,u+radius)){
                if(P_GlobalDigBreachMargin(line,side,(float)v,(float)h)<=.02f){clear=0;break;}
                if(v>=u+radius)break;
            }
        if(clear)return base;
    }return -1e20;
}
static int breachFit(int line,int side,mobj_t *thing,fixed_t x,fixed_t y){
    return breachFitHeight(line,side,thing,x,y)>-1e19;
}
fixed_t P_GlobalDigBreachStepFloor(mobj_t *thing,fixed_t x,fixed_t y,fixed_t floor){
    if(!P_GlobalDigThroughEnabled()||thing->flags&(MF_MISSILE|MF_FLOAT|MF_SKULLFLY))return floor;
    indexBreaches();
    for(int i=0;i<breachCount;++i){breach_t *b=&breaches[i];
        double ax,ay,tx,ty,len=frame(b->source,b->side,&ax,&ay,&tx,&ty);
        double u=(unit(x)-ax)*tx+(unit(y)-ay)*ty,d=-(unit(x)-ax)*ty+(unit(y)-ay)*tx,r=unit(thing->radius);
        if(u<0||u>len||d+r<=0||d-r>=b->depth)continue;
        double support=breachFitHeight(b->source,b->side,thing,x,y);
        // Never turn airborne height into a new floor.
        if(support>unit(floor)&&support<=unit(thing->z)+24&&thing->z<=thing->floorz+FRACUNIT)
            floor=(fixed_t)(support*FRACUNIT);
    }return floor;
}
int P_GlobalDigBreachRay(int line,float x,float y,float z,float dx,float dy,float dz){
    for(int side=0;side<2;++side)for(int r=firstBreach(line,side);r>=0;r=breachRefs[r].next){breach_t *b=&breaches[breachRefs[r].link];
        double ax,ay,tx,ty;frame(b->source,b->side,&ax,&ay,&tx,&ty);
        double normal=-dx*ty+dy*tx;if(fabs(normal)<1e-8)continue;
        double u=(x-ax)*tx+(y-ay)*ty,d=-(x-ax)*ty+(y-ay)*tx;
        // Validate the whole angled path through the slab, in either direction.
        int clear=1;for(float v=0;;v=fminf(v+2,b->depth)){
            double t=(v-d)/normal;
            if(breachMargin(b,(float)(u+t*(dx*tx+dy*ty)),(float)(z+t*dz))<=.02f){clear=0;break;}
            if(v>=b->depth)break;
        }if(clear)return 1;
    }return 0;
}
int P_GlobalDigBreachSector(int line,float x,float y,float dx,float dy){
    for(int side=0;side<2;++side)for(int r=firstBreach(line,side);r>=0;r=breachRefs[r].next){breach_t *b=&breaches[breachRefs[r].link];double ax,ay,tx,ty;frame(b->source,b->side,&ax,&ay,&tx,&ty);
        return (int)((-dx*ty+dy*tx>0?breachRoom(b,1):breachRoom(b,0))-sectors);
    }return -1;
}

// A breached closed slab exposes the supporting room floor. Its coincident
// ceiling must not remain as a dark, double-sided sheet across the opening.
int P_GlobalDigBreachSupportRoom(int sector){
    if(!P_GlobalDigThroughEnabled()||sector<0||sector>=numsectors||sectors[sector].floorheight!=sectors[sector].ceilingheight)return -1;
    indexBreaches();
    for(int i=0;i<breachCount;++i){breach_t *b=&breaches[i];
        if(b->solidSector!=sector){
            if(breachRoom(b,0)!=&sectors[sector]&&breachRoom(b,1)!=&sectors[sector])continue;
            sector_t *room=&sectors[sector];
            for(int j=0;j<room->linecount;++j){line_t *l=room->lines[j];sector_t *other=l->frontsector==room?l->backsector:l->frontsector;
                if(other&&other->ceilingheight>other->floorheight&&other->floorheight==room->floorheight)return (int)(other-sectors);
            }
            continue;
        }
        sector_t *a=breachRoom(b,0),*e=breachRoom(b,1);
        sector_t *support=a->floorheight>=e->floorheight?a:e;
        if(support->floorheight==sectors[sector].floorheight)return (int)(support-sectors);
    }return -1;
}

// Excavation exposes the inside of a door slab above its moving underside.
// Preserve its perimeter walls throughout the exposed room-height interval.
void P_GlobalDigBreachRoomBounds(int sector,float *floor,float *ceiling){
    if(!P_GlobalDigThroughEnabled())return;
    indexBreaches();
    for(int i=0;i<breachCount;++i){breach_t *b=&breaches[i];
        sector_t *a=breachRoom(b,0),*e=breachRoom(b,1);
        if(b->solidSector!=sector){
            if((a==&sectors[sector]||e==&sectors[sector])&&sectors[sector].ceilingheight<=sectors[sector].floorheight){
                float low,high;breachRoomHeights(&sectors[sector],&low,&high);*floor=fminf(*floor,low);*ceiling=fmaxf(*ceiling,high);
            }continue;
        }
        float af,ac,ef,ec;breachRoomHeights(a,&af,&ac);breachRoomHeights(e,&ef,&ec);
        *floor=fminf(*floor,fmaxf(af,ef));*ceiling=fmaxf(*ceiling,fminf(ac,ec));
    }
}
