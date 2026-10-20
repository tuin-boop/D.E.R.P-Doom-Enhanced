#ifndef P_DIGWALL_H
#define P_DIGWALL_H
#ifdef __cplusplus
extern "C" {
#endif
int P_GlobalDigEnabled(void);
extern int p_monster_surface_damage,p_player_surface_damage;
void P_GlobalDigBulletWall(mobj_t *shooter,int line,int side,fixed_t x,fixed_t y,fixed_t z);
void P_GlobalDigBulletFlat(mobj_t *shooter,int sector,int ceiling,fixed_t x,fixed_t y);
extern int p_throughwalls,p_breachdoors,p_wall_damage,p_deeperdig;
int P_GlobalDigThroughEnabled(void);
int P_GlobalDigMaxDepth(void);
float P_GlobalDigVoidMargin(int line,float x,float y,float z);
float P_GlobalDigBreachMargin(int line,int side,float u,float z);
float P_GlobalDigBreachLimit(int line,int side,float u,float z);
int P_GlobalDigBreachRay(int line,float x,float y,float z,float dx,float dy,float dz);
int P_GlobalDigBreachFace(int line,int side);
int P_GlobalDigBreachSector(int line,float x,float y,float dx,float dy);

float P_GlobalDigWallLimit(int line,int side,float u);
int P_GlobalDigSolidFlat(int sector,int ceiling);
int P_GlobalDigFlatBounds(int sector,int ceiling,float *bounds);
float P_GlobalDigFlatDepth(int sector,int ceiling,float x,float y);
void P_GlobalDigFlatImpact(mobj_t *rocket,int ceiling);
void P_GlobalDigFlatHeights(mobj_t *thing,fixed_t x,fixed_t y,int sector,fixed_t *floor,fixed_t *ceiling);
void P_GlobalDigRefreshHeights(mobj_t *thing);
void P_GlobalDigReset(void);
int P_GlobalDigImpact(mobj_t *rocket);
void P_GlobalDigBarrelBlast(mobj_t *barrel);
void P_GlobalDigMonsterContact(mobj_t *rocket,fixed_t x,fixed_t y);
void P_GlobalDigBreachRoomBounds(int sector,float *floor,float *ceiling);
int P_GlobalDigBreachSupportRoom(int sector);
fixed_t P_GlobalDigBreachStepFloor(mobj_t *thing,fixed_t x,fixed_t y,fixed_t floor);
int P_GlobalDigRetreat(int line,int side,mobj_t *thing,fixed_t x,fixed_t y);
int P_GlobalDigFit(int line,int side,mobj_t *thing,fixed_t x,fixed_t y);
int P_GlobalDigPosition(mobj_t *thing,fixed_t x,fixed_t y,int *sector);
int P_GlobalDigWallJoined(int line,int side,int end);
void P_GlobalDigWallPoint(int line,int side,float u,float z,float depth,float *x,float *y);
float P_GlobalDigDepth(int line,int side,float u,float z);
int P_GlobalDigBounds(int line,int side,float *bounds);
unsigned P_GlobalDigRevision(void);
unsigned P_GlobalDigWallRevision(int line,int side);
unsigned P_GlobalDigFlatRevision(int sector,int ceiling);
unsigned P_GlobalDigFlatPatch(int sector,int ceiling,const float *area,float *bounds);
int P_GlobalDigSaveSize(void);
int P_GlobalDigSave(unsigned char *data);
void P_GlobalDigLoad(const unsigned char *data,int length);
#ifdef __cplusplus
}
#endif
#endif
