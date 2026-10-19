/* Local floor/ceiling craters. Surface heights stay relative to moving sectors. */
#include "r_sky.h"
#include "w_wad.h"
int P_GlobalDigSolidFlat(int sector,int ceiling){
    if(sector<0||sector>=numsectors)return 0;
    int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
    if(pic==skyflatnum)return 0;
    const char *name=lumpinfo[firstflat+pic].name;
    // Same liquid families used by the renderer's splash/shoreline system.
    return strncmp(name,"NUKAGE",6)&&strncmp(name,"FWATER",6)&&strncmp(name,"SLIME",5)&&strncmp(name,"BLOOD",5)&&strncmp(name,"LAVA",4);
}
#define MAX_FLAT_CUTS 2048
typedef struct {int sector,ceiling;float x,y,strength;} flatcut_t;
static flatcut_t flatCuts[MAX_FLAT_CUTS];
static int flatCount,flatChanged;
static mobj_t *flatMissile;
static int *flatHeads,flatNext[MAX_FLAT_CUTS];
static float *flatBounds;
static unsigned flatIndexRevision;
static unsigned flatVersions[MAX_FLAT_CUTS];
void P_GlobalDigFlatReset(void){flatCount=0;flatMissile=NULL;free(flatHeads);free(flatBounds);flatHeads=NULL;flatBounds=NULL;flatIndexRevision=0;}
static void indexFlats(void){
    if(flatIndexRevision==revision&&flatHeads)return;
    if(!flatHeads){flatHeads=malloc(numsectors*2*sizeof(int));flatBounds=malloc(numsectors*8*sizeof(float));if(!flatHeads||!flatBounds)I_Error("Flat excavation allocation failed");}
    for(int i=0;i<numsectors*2;++i){flatHeads[i]=-1;float *b=flatBounds+i*4;b[0]=b[2]=1e9f;b[1]=b[3]=-1e9f;}
    for(int i=0;i<flatCount;++i){flatcut_t *c=&flatCuts[i];int key=c->sector*2+c->ceiling;flatNext[i]=flatHeads[key];flatHeads[key]=i;float *b=flatBounds+key*4;
        b[0]=fminf(b[0],c->x-32);b[1]=fmaxf(b[1],c->x+32);b[2]=fminf(b[2],c->y-32);b[3]=fmaxf(b[3],c->y+32);}
    flatIndexRevision=revision;
}
int P_GlobalDigFlatBounds(int sector,int ceiling,float *bounds){
    if(!P_GlobalDigEnabled()||!flatCount||!P_GlobalDigSolidFlat(sector,ceiling))return 0;
    indexFlats();if(flatHeads[sector*2+ceiling]<0)return 0;
    memcpy(bounds,flatBounds+(sector*2+ceiling)*4,4*sizeof(float));return 1;
}
// A sector can cover a whole arena. Only leaves touched by a cut need detail.
unsigned P_GlobalDigFlatPatch(int sector,int ceiling,const float *area,float *bounds){
    if(!P_GlobalDigEnabled()||!flatCount||!P_GlobalDigSolidFlat(sector,ceiling))return 0;
    indexFlats();unsigned value=0;bounds[0]=bounds[2]=1e9f;bounds[1]=bounds[3]=-1e9f;
    for(int i=flatHeads[sector*2+ceiling];i>=0;i=flatNext[i]){flatcut_t *c=&flatCuts[i];
        if(c->x+32<area[0]||c->x-32>area[1]||c->y+32<area[2]||c->y-32>area[3])continue;
        if(flatVersions[i]>value)value=flatVersions[i];
        bounds[0]=fminf(bounds[0],c->x-32);bounds[1]=fmaxf(bounds[1],c->x+32);
        bounds[2]=fminf(bounds[2],c->y-32);bounds[3]=fmaxf(bounds[3],c->y+32);
    }return value;
}
static float flatLatticeUncached(int sector,int ceiling,int x,int y){
    float d=0;indexFlats();
    for(int i=flatHeads[sector*2+ceiling];i>=0;i=flatNext[i]){flatcut_t *c=&flatCuts[i];
        float dx=(x-c->x)/24,dy=(y-c->y)/24;
        if(fabsf(dx)>1.2f||fabsf(dy)>1.2f)continue;
        float angle=atan2f(dy,dx),phase=c->x*.17f+c->y*.13f;
        float edge=1+.12f*sinf(angle*5+phase)+.07f*cosf(angle*9-phase),r=(dx*dx+dy*dy)/(edge*edge);
        if(r<1)d+=c->strength*(1-r);
    }
    if(d<=0)return 0;
    // Seal against walls, steps and sector boundaries; no floating rim caps.
    sector_t *s=&sectors[sector];double border=.75;
    for(int i=0;i<s->linecount;++i){line_t *l=s->lines[i];if(l->frontsector==l->backsector)continue;
        sector_t *other=l->frontsector==s?l->backsector:l->frontsector;
        if(other&&(ceiling?other->ceilingheight==s->ceilingheight:other->floorheight==s->floorheight))continue;
        double ax=unit(l->v1->x),ay=unit(l->v1->y),dx=unit(l->v2->x-l->v1->x),dy=unit(l->v2->y-l->v1->y),len=dx*dx+dy*dy;
        double t=len>0?fmax(0,fmin(1,((x-ax)*dx+(y-ay)*dy)/len)):0;
        border=fmin(border,hypot(x-ax-dx*t,y-ay-dy*t));
    }
    unsigned hash=(unsigned)x*73856093u^(unsigned)y*19349663u^(unsigned)sector*83492791u;
    d*=.85f+(hash%101)*.003f;
    return fminf(d,ceiling?32.f:16.f)*(float)(border/.75);
}
static float flatLattice(int sector,int ceiling,int x,int y){
    typedef struct {int sector,ceiling,x,y,tic;unsigned revision;float value;} Sample;
    static Sample cache[65536];
    unsigned key=((unsigned)sector*73856093u^(unsigned)ceiling*83492791u^(unsigned)x*19349663u^(unsigned)y*2654435761u)&65535;
    Sample *c=&cache[key];
    if(c->revision!=revision||c->tic!=gametic||c->sector!=sector||c->ceiling!=ceiling||c->x!=x||c->y!=y)
        *c=(Sample){sector,ceiling,x,y,gametic,revision,flatLatticeUncached(sector,ceiling,x,y)};
    return c->value;
}
unsigned P_GlobalDigFlatRevision(int sector,int ceiling){
    indexFlats();unsigned value=0;
    for(int i=flatHeads[sector*2+ceiling];i>=0;i=flatNext[i])if(flatVersions[i]>value)value=flatVersions[i];
    return value;
}
float P_GlobalDigFlatDepth(int sector,int ceiling,float x,float y){
    if(!P_GlobalDigEnabled()||!flatCount||!P_GlobalDigSolidFlat(sector,ceiling))return 0;
    indexFlats();float *bounds=flatBounds+(sector*2+ceiling)*4;
    if(x<bounds[0]||x>bounds[1]||y<bounds[2]||y>bounds[3])return 0;
    int u=(int)floorf(x/4)*4,v=(int)floorf(y/4)*4;float a=(x-u)/4,b=(y-v)/4;
    float p=flatLattice(sector,ceiling,u,v),q=flatLattice(sector,ceiling,u+4,v+4);
    return a>=b?p*(1-a)+flatLattice(sector,ceiling,u+4,v)*(a-b)+q*b:p*(1-b)+flatLattice(sector,ceiling,u,v+4)*(b-a)+q*a;
}
static void addFlatCut(int sector,int ceiling,float x,float y){
    if(!P_GlobalDigSolidFlat(sector,ceiling))return;
    for(int i=0;i<flatCount;++i){flatcut_t *c=&flatCuts[i];
        if(c->sector==sector&&c->ceiling==ceiling&&fabsf(c->x-x)<4&&fabsf(c->y-y)<4){c->strength=fminf(256,c->strength+impactStrength);flatVersions[i]=++revision;return;}}
    if(flatCount==MAX_FLAT_CUTS)return;
    flatVersions[flatCount]=revision+1;flatCuts[flatCount++]=(flatcut_t){sector,ceiling,x,y,impactStrength};++revision;
}
static void spreadFlatCuts(int sector,int ceiling,float x,float y){
    int queue[64]={sector},size=1;
    fixed_t height=ceiling?sectors[sector].ceilingheight:sectors[sector].floorheight;
    for(int q=0;q<size;++q){sector_t *s=&sectors[queue[q]];addFlatCut(queue[q],ceiling,x,y);
        for(int i=0;i<s->linecount&&size<64;++i){line_t *l=s->lines[i];sector_t *other=l->frontsector==s?l->backsector:l->frontsector;
            if(!other||other==s||!P_GlobalDigSolidFlat((int)(other-sectors),ceiling)||(ceiling?other->ceilingheight:other->floorheight)!=height||(ceiling?other->ceilingpic:other->floorpic)==skyflatnum)continue;
            int id=(int)(other-sectors),seen=0;for(int n=0;n<size;++n)if(queue[n]==id)seen=1;if(seen)continue;
            double ax=unit(l->v1->x),ay=unit(l->v1->y),dx=unit(l->v2->x-l->v1->x),dy=unit(l->v2->y-l->v1->y),len=dx*dx+dy*dy;
            double t=len>0?fmax(0,fmin(1,((x-ax)*dx+(y-ay)*dy)/len)):0;
            if(hypot(x-ax-t*dx,y-ay-t*dy)<28)queue[size++]=id;
        }
    }
}
void P_GlobalDigFlatImpact(mobj_t *rocket,int ceiling){
    if(!P_GlobalDigEnabled()||rocket->type!=MT_ROCKET)return;
    flatMissile=rocket;flatChanged=0;
    sector_t *s=rocket->subsector->sector;int sector=(int)(s-sectors);
    if(!P_GlobalDigSolidFlat(sector,ceiling))return;
    float x=(float)unit(rocket->x),y=(float)unit(rocket->y);
    spreadFlatCuts(sector,ceiling,x,y);spreadFlatToWalls(sector,ceiling,x,y);flatChanged=1;
}
void P_GlobalDigFlatHeights(mobj_t *thing,fixed_t x,fixed_t y,int sector,fixed_t *floor,fixed_t *ceiling){
    if(!P_GlobalDigEnabled()||!flatCount)return;
    indexFlats();
    for(int c=0;c<2;++c){if(flatHeads[sector*2+c]<0)continue;
        float depth=P_GlobalDigFlatDepth(sector,c,(float)unit(x),(float)unit(y)),r=(float)unit(thing->radius);
        if(depth<=0)continue;
        for(int i=-1;i<=1&&depth>0;++i)for(int j=-1;j<=1&&depth>0;++j)
            depth=fminf(depth,P_GlobalDigFlatDepth(sector,c,(float)unit(x)+i*r,(float)unit(y)+j*r));
        if(c)*ceiling+=(fixed_t)(depth*FRACUNIT);else *floor-=(fixed_t)(depth*FRACUNIT);
    }
}
void P_GlobalDigRefreshHeights(mobj_t *thing){
    if(!flatCount||!thing->subsector||thing->flags&MF_NOCLIP)return;
    indexFlats();int s=(int)(thing->subsector->sector-sectors);
    if(flatHeads[s*2]<0&&flatHeads[s*2+1]<0)return;
    float area[4]={(float)unit(thing->x-thing->radius),(float)unit(thing->x+thing->radius),
                   (float)unit(thing->y-thing->radius),(float)unit(thing->y+thing->radius)},bounds[4];
    if(thing->floorz==sectors[s].floorheight&&thing->ceilingz==sectors[s].ceilingheight&&
       !P_GlobalDigFlatPatch(s,0,area,bounds)&&!P_GlobalDigFlatPatch(s,1,area,bounds))return;
    // Recheck stationary actors too: a newly excavated crater changes support.
    P_CheckPosition(thing,thing->x,thing->y);
    thing->floorz=tmfloorz;thing->ceilingz=tmceilingz;
}
