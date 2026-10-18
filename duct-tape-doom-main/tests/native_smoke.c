/* Exercise an actual level and complete native save/load without desktop input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_state.h"
#include "r_main.h"
#include "g_mapinfo.h"
#include "s_sound.h"
#include "sounds.h"
#include "p_local.h"
#include "p_fluid.h"
#include "p_notices.h"
#include "m_bbox.h"
#include "d_items.h"
#ifdef DOOM_RENDER3D
#include "../platform/i_render3d.h"
#include "../platform/mod_soundbank.h"
#endif
extern void A_PosAttack(mobj_t *actor);
extern void P_XYMovement(mobj_t *actor);
extern unsigned I_NamedSoundStarts(void);
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int notice_count(int kind){noticeevent_t e;int n=0;while(P_PopNotice(0,&e))if(e.kind==kind)++n;return n;}
static void test_notices(void){
    player_t* p=&players[0];int oldtime=leveltime,oldkills=p->killcount,oldhealth=p->health;
    int oldweapon=p->readyweapon;unsigned char saved[NOTICE_SAVE_BYTES];
    P_NoticeNewGame();P_NoticeLevel();
    P_NoticeWeapon(p,wp_bfg);P_NoticeWeapon(p,wp_bfg);
    if(notice_count(N_WEAPON)!=1)I_Error("Weapon discovery did not deduplicate");
    if(P_SaveNotices(saved)!=NOTICE_SAVE_BYTES)I_Error("Notice save size");
    P_NoticeNewGame();P_LoadNotices(saved,sizeof(saved));P_NoticeWeapon(p,wp_bfg);
    if(notice_count(N_WEAPON))I_Error("Saved weapon discovery replayed");
    for(int k=0;k<N_COUNT;++k){int mask=0;noticeevent_t e;
        for(int v=0;v<3;++v)P_Notice(0,k,0);
        while(P_PopNotice(0,&e))mask|=1<<e.variant;
        if(mask!=7)I_Error("Missing notice variant %d",k);
    }
    mobj_t* boss=P_SpawnMobj(p->mo->x+256*FRACUNIT,p->mo->y,ONFLOORZ,MT_CYBORG);
    P_DamageMobj(boss,p->mo,p->mo,100000);P_RemoveMobj(boss);
    if(notice_count(N_CYBER)!=1)I_Error("Cyberdemon kill notice hook");
    boss=P_SpawnMobj(p->mo->x+256*FRACUNIT,p->mo->y,ONFLOORZ,MT_SPIDER);
    P_DamageMobj(boss,p->mo,p->mo,100000);P_RemoveMobj(boss);
    if(notice_count(N_SPIDER)!=1)I_Error("Mastermind kill notice hook");
    P_NoticeLevel();mobj_t target={0},barrel={0};target.info=&mobjinfo[MT_BRUISER];target.type=MT_BRUISER;barrel.type=MT_BARREL;
    for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    noticeevent_t e;int chain=0,multi=0;while(P_PopNotice(0,&e)){chain+=e.kind==N_CHAIN;multi+=e.kind==N_MULTI;}
    if(chain!=1||multi!=1)I_Error("Barrel and multi-kill grouping");
    leveltime++;for(int n=0;n<3;++n)P_NoticeKill(p->mo,&target,&barrel);
    if(P_PopNotice(0,&e))I_Error("Combat notice cooldown");
    p->readyweapon=wp_fist;P_NoticeKill(p->mo,&target,p->mo);
    if(notice_count(N_FISTS)!=1)I_Error("Tough fist kill notice");
    P_NoticeLevel();p->health=5;P_NoticeDamage(p,1);leveltime=((leveltime/35)+6)*35;
    P_NoticeTick(p);if(notice_count(N_CLOSE)!=1)I_Error("Close call after combat");
    p->health=oldhealth;p->killcount=oldkills;p->readyweapon=oldweapon;leveltime=oldtime;
    P_NoticeNewGame();P_NoticeLevel();
    puts("Special notices: variants, save persistence, boss hooks, combat grouping and cooldown passed.");
}

static int stage, saved_ammo, saved_health, player_flashes, enemy_flashes;
static int test_weapon=wp_pistol,test_ammo=am_clip;
static int test_episode=1,test_start;
static int routed_sounds;
static fixed_t aim_velocity[3];static int aim_exploded;
static void capture_aim_impact(mobj_t *m){aim_velocity[0]=m->momx;aim_velocity[1]=m->momy;aim_velocity[2]=m->momz;aim_exploded=1;}
static void test_projectile_pitch(void){
    boolean oldfree=freeaim;float oldpitch=tourpitch;void (*oldhook)(mobj_t*)=R_RocketImpact;
    R_RocketImpact=capture_aim_impact;freeaim=true;
    for(int kind=0;kind<3;++kind)for(int dir=-1;dir<=1;++dir){
        mobjtype_t type=kind==0?MT_ROCKET:kind==1?MT_PLASMA:MT_BFG;
        tourpitch=dir*.65f;aim_exploded=0;P_SpawnPlayerMissile(players[0].mo,type);
        mobj_t *m=(mobj_t*)thinkercap.prev;
        if(m->type!=type||m->target!=players[0].mo)I_Error("Missing aimed projectile");
        if(!aim_exploded){aim_velocity[0]=m->momx;aim_velocity[1]=m->momy;aim_velocity[2]=m->momz;}
        if((dir>0&&aim_velocity[2]<=0)||(dir<0&&aim_velocity[2]>=0)||(dir==0&&aim_velocity[2]!=0))I_Error("Projectile did not follow vertical aim");
        double speed=sqrt((double)aim_velocity[0]*aim_velocity[0]+(double)aim_velocity[1]*aim_velocity[1]+(double)aim_velocity[2]*aim_velocity[2]);
        if(fabs(speed-m->info->speed)>FRACUNIT*.02)I_Error("Aimed projectile changed total speed");
        P_RemoveMobj(m);
    }
    R_RocketImpact=oldhook;freeaim=oldfree;tourpitch=oldpitch;
    puts("Rocket/plasma/BFG upward, downward and level aim preserves projectile speed.");
}
static void test_fast_pistol(void){
    if(!M_CheckParm("-fluidweapons"))return;
    extern int p_fluidfastpistol;
    int setting=p_fluidfastpistol;
    player_t *p=&players[0];
    for(int fast=0;fast<=1;++fast){
        p_fluidfastpistol=fast;p->readyweapon=wp_pistol;p->pendingweapon=wp_nochange;
        p->weaponowned[wp_pistol]=true;p->ammo[am_clip]=50;p->cmd.buttons=BT_ATTACK;
        P_SetupPsprites(p);P_FireWeapon(p);
        for(int tic=0;tic<30;++tic)P_MovePsprites(p);
        if(p->ammo[am_clip]!=(fast?46:47))I_Error("Pistol cadence mismatch fast=%d ammo=%d",fast,p->ammo[am_clip]);
    }
    p->cmd.buttons=0;p_fluidfastpistol=setting;P_SetupPsprites(p);
    p->readyweapon=wp_missile;p->weaponowned[wp_missile]=true;p->ammo[am_misl]=10;P_SetupPsprites(p);P_FireWeapon(p);
    if(p->psprites[ps_weapon].tics!=2||p->ammo[am_misl]!=10)I_Error("Fluid rocket windup mismatch");
    P_MovePsprites(p);if(p->ammo[am_misl]!=10)I_Error("Rocket fired before its source tick");
    P_MovePsprites(p);if(p->ammo[am_misl]!=9||p->psprites[ps_weapon].tics!=12)I_Error("Rocket launch or recovery timing mismatch");
    p->readyweapon=wp_pistol;P_SetupPsprites(p);
    puts("Pistol: optional 10-tic fast and 14-tic normal firing cadence passed.");
}
static int aggressiveShotsCount,aggressiveShotTimes[8];
static mobj_t *aggressiveTestActor;
static void aggressive_flash(mobj_t *actor,int weapon,int projectile){
    if(actor==aggressiveTestActor&&aggressiveShotsCount<8)aggressiveShotTimes[aggressiveShotsCount++]=leveltime;
}
static void test_aggressive_ai(void){
    extern int p_fluidfastpistol;
    extern boolean P_CheckMissileRange(mobj_t *actor);
    int enabled=p_aggressiveai,oldtime=leveltime,fast=p_fluidfastpistol;
    fixed_t laneX=0,laneY=0;
    player_t *player=&players[0];mobj_t *target=player->mo;
    int cheats=player->cheats;fixed_t mx=target->momx,my=target->momy;
    void (*flash)(mobj_t*,int,int)=R_MuzzleFlash;
    P_SetAggressiveAI(1);player->cheats|=CF_GODMODE;R_MuzzleFlash=aggressive_flash;
    for(int mode=0;mode<3;++mode){
        mobj_t *actor=NULL;p_fluidfastpistol=mode==1;
        for(int direction=0;direction<4;++direction){
            fixed_t x=target->x+(direction==0?64:direction==1?-64:0)*FRACUNIT;
            fixed_t y=target->y+(direction==2?64:direction==3?-64:0)*FRACUNIT;
            actor=P_SpawnMobj(x,y,ONFLOORZ,mode==2?MT_SHOTGUY:MT_POSSESSED);actor->target=target;actor->reactiontime=0;
            if(P_CheckPosition(actor,x,y)&&P_CheckMissileRange(actor))break;
            P_RemoveMobj(actor);actor=NULL;
        }
        if(!actor)I_Error("No clear firing lane for aggressive AI test");
        laneX=actor->x;laneY=actor->y;
        aggressiveTestActor=actor;aggressiveShotsCount=0;
        P_SetMobjState(actor,actor->info->missilestate);
        for(int tic=0;tic<180&&aggressiveShotsCount<4;++tic){++leveltime;P_MobjThinker(actor);}
        int expected=mode==2?37:(mode==1&&P_FluidWeaponsEnabled()?10:14);
        if(aggressiveShotsCount<4)I_Error("Aggressive shooter stopped firing mode %d",mode);
        for(int n=1;n<4;++n)if(aggressiveShotTimes[n]-aggressiveShotTimes[n-1]!=expected)I_Error("Aggressive cadence mode %d: %d vs %d",mode,aggressiveShotTimes[n]-aggressiveShotTimes[n-1],expected);
        // Real projectile feedback: a miss requests a movement burst; a hit does not.
        P_AggressiveRemove(actor);mobj_t missile={0};P_AggressiveMissile(&missile,actor);P_AggressiveImpact(&missile);
        if(P_CheckMissileRange(actor))I_Error("Missed projectile did not request repositioning");
        P_AggressiveRemove(actor);P_AggressiveMissile(&missile,actor);P_AggressiveContact(target,&missile,actor);P_AggressiveImpact(&missile);
        if(!P_CheckMissileRange(actor))I_Error("Successful projectile incorrectly requested repositioning");
        actor->health=100;P_AggressiveDamage(actor,target,target,40);
        if(P_CheckMissileRange(actor))I_Error("Heavy damage did not stagger shooter");
        P_RemoveMobj(actor);
    }
    {
        extern void A_Chase(mobj_t*);
        mobj_t *fat=P_SpawnMobj(laneX,laneY,ONFLOORZ,MT_FATSO),aim;
        fat->target=target;fat->reactiontime=0;fat->threshold=100;
        for(int volley=0;volley<3;++volley)A_Chase(fat);
        if(!P_AggressiveBarrageAim(fat,&aim))I_Error("Mancubus did not start occasional barrage");
        fixed_t originalX=target->x;target->x+=128*FRACUNIT;
        if(!P_AggressiveBarrageAim(fat,&aim)||aim.x!=originalX)I_Error("Barrage incorrectly tracks moving target");
        target->x=originalX;leveltime+=161;
        if(P_AggressiveBarrageAim(fat,&aim))I_Error("Mancubus barrage did not expire");
        P_RemoveMobj(fat);
    }
    demoplayback=true;if(P_AggressiveEnabled())I_Error("Aggressive AI changed demos");demoplayback=false;
    P_SetAggressiveAI(0);if(P_AggressiveStateTics(target,S_POSS_ATK1,10)!=10)I_Error("Disabled AI changed animation timings");
    P_SetAggressiveAI(enabled);leveltime=oldtime;p_fluidfastpistol=fast;player->cheats=cheats;target->momx=mx;target->momy=my;R_MuzzleFlash=flash;aggressiveTestActor=NULL;
    puts("Aggressive AI: live pistol/shotgun cadence, projectile hits/misses, heavy-hit stagger, fixed-position barrages and demo guards passed.");
}
static void test_advanced_tactics(void){
    int setting=p_advancedtactics,oldtime=leveltime;P_SetAdvancedTactics(1);
    mobj_t target={0},actor={0},aim;target.health=100;target.flags=MF_SHOOTABLE;
    target.x=512*FRACUNIT;target.momx=12*FRACUNIT;
    actor.health=100;actor.flags=MF_COUNTKILL;actor.target=&target;actor.type=MT_CYBORG;
    P_TacticsFired(&actor);
    if(!P_TacticsAim(&actor,&aim)||aim.x<=target.x||aim.x>target.x+96*FRACUNIT)I_Error("Predictive rocket aim not bounded");
    P_TacticsRemove(&actor);actor.type=MT_FATSO;
    if(!P_TacticsAim(&actor,&aim))I_Error("Focused mancubus volley missing");
    fixed_t locked=aim.x;target.x+=128*FRACUNIT;P_TacticsFired(&actor);
    if(!P_TacticsAim(&actor,&aim)||aim.x!=locked)I_Error("Focused volley followed target");
    angle_t focused=P_TacticsFatSpread(&actor);
    for(int n=1;n<6;++n)P_TacticsFired(&actor);
    if(P_TacticsFatSpread(&actor)<=focused)I_Error("Mancubus did not alternate wide volley");
    P_TacticsRemove(&actor);
    // Exercise safe vertical motion in an actual loaded sector.
    mobj_t *player=players[0].mo,*caco=P_SpawnMobj(player->x+64*FRACUNIT,player->y,ONFLOORZ,MT_HEAD);
    caco->target=player;caco->z=caco->ceilingz-caco->height-8*FRACUNIT;
    fixed_t before=caco->z;leveltime=(leveltime+1)&~1;P_TacticsTick(caco);
    if(caco->z+caco->height>caco->ceilingz||caco->z<caco->floorz||abs(caco->z-before)>FRACUNIT)I_Error("Cacodemon tactics exceeded clearance or speed");
    P_RemoveMobj(caco);
    // Lost souls lack MF_COUNTKILL. Exercise the real decoration collision path,
    // including aggressive attack gating and a direct charge action retry.
    {
        extern mobj_t *tmthing;extern fixed_t tmx,tmy;
        extern boolean PIT_CheckThing(mobj_t*),P_CheckMissileRange(mobj_t*);
        extern void A_SkullAttack(mobj_t*);
        mobj_t *skull=P_SpawnMobj(player->x+64*FRACUNIT,player->y,ONFLOORZ,MT_SKULL);
        mobj_t torch={0};torch.flags=MF_SOLID;torch.radius=16*FRACUNIT;torch.health=1000;
        torch.x=skull->x;torch.y=skull->y;
        mobj_t *savedThing=tmthing;fixed_t savedX=tmx,savedY=tmy;
        skull->target=player;skull->flags|=MF_SKULLFLY;skull->momx=20*FRACUNIT;
        tmthing=skull;tmx=skull->x;tmy=skull->y;
        if(PIT_CheckThing(&torch)||!P_TacticsAttackBlocked(skull)||skull->flags&MF_SKULLFLY)
            I_Error("Lost soul did not recover from solid decoration");
        int aggressive=p_aggressiveai;P_SetAggressiveAI(1);
        if(P_CheckMissileRange(skull))I_Error("Aggressive AI bypassed charge recovery");
        A_SkullAttack(skull);
        if(skull->flags&MF_SKULLFLY)I_Error("Direct charge bypassed recovery");
        leveltime+=2*TICRATE;
        if(P_TacticsAttackBlocked(skull))I_Error("Charge recovery never expired");
        P_SetAdvancedTactics(0);P_TacticsChargeBlocked(skull);
        if(P_TacticsAttackBlocked(skull))I_Error("Disabled tactics changed lost soul");
        P_SetAdvancedTactics(1);P_SetAggressiveAI(aggressive);
        tmthing=savedThing;tmx=savedX;tmy=savedY;P_RemoveMobj(skull);
    }
    demoplayback=true;if(P_TacticsEnabled())I_Error("Advanced tactics changed demo behavior");demoplayback=false;
    actor.target=NULL;if(P_TacticsMove(&actor)||P_TacticsAim(&actor,&aim))I_Error("Tactics woke unaware monster");
    P_SetAdvancedTactics(0);actor.target=&target;
    if(P_TacticsAim(&actor,&aim)||P_TacticsFatSpread(&actor)!=ANG90/8)I_Error("Disabled tactics changed attacks");
    P_SetAdvancedTactics(setting);leveltime=oldtime;
    puts("Advanced tactics: bounded prediction, alternating/fixed volleys, aerial clearance and disabled/demo guards passed.");
}
static void test_pursuit_guards(void){
    extern int prndindex;
    int setting=p_persistentpursuit,random=prndindex;
    mobj_t actor={0},target={0};actor.flags=MF_COUNTKILL;actor.target=&target;
    p_persistentpursuit=0;
    if(P_PersistentPursuit(&actor))I_Error("Disabled pursuit changed movement");
    p_persistentpursuit=1;demoplayback=true;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit changed demo playback");
    demoplayback=false;demorecording=true;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit changed demo recording");
    demorecording=false;netgame=true;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit changed network movement");
    netgame=false;actor.target=NULL;
    if(P_PersistentPursuit(&actor))I_Error("Pursuit woke an unaware monster");
    if(actor.x||actor.y||prndindex!=random)I_Error("Pursuit guard changed state or random stream");
    p_persistentpursuit=setting;
    puts("Persistent pursuit: toggle, unaware monsters, demos, network and RNG guards passed.");
}
static void test_overlap_escape(void){
    extern mobj_t *tmthing;extern fixed_t tmx,tmy;extern boolean PIT_CheckThing(mobj_t*);
    mobj_t a={0},b={0};mobj_t *previous=tmthing;
    a.flags=b.flags=MF_COUNTKILL|MF_SOLID;a.health=b.health=150;
    a.radius=b.radius=30*FRACUNIT;b.x=b.y=48*FRACUNIT;tmthing=&a;
    tmx=0;tmy=-3*FRACUNIT;if(!PIT_CheckThing(&b))I_Error("Overlapping monsters cannot separate");
    tmy=3*FRACUNIT;if(PIT_CheckThing(&b))I_Error("Overlap escape allowed a deeper overlap");
    b.x=61*FRACUNIT;tmx=3*FRACUNIT;tmy=0;if(PIT_CheckThing(&b))I_Error("Overlap escape allowed a new collision");
    b.x=48*FRACUNIT;tmx=0;tmy=-3*FRACUNIT;demoplayback=true;
    if(PIT_CheckThing(&b))I_Error("Overlap escape changed demo behavior");demoplayback=false;
    {
        extern fixed_t tmbbox[4];extern boolean PIT_CheckLine(line_t*);
        vertex_t v1={0,0},v2={0,100*FRACUNIT};line_t line={0};
        line.v1=&v1;line.v2=&v2;line.dy=100*FRACUNIT;line.slopetype=ST_VERTICAL;
        line.bbox[BOXBOTTOM]=0;line.bbox[BOXTOP]=100*FRACUNIT;
        a.x=16*FRACUNIT;a.y=50*FRACUNIT;tmx=19*FRACUNIT;tmy=a.y;
        tmbbox[BOXLEFT]=tmx-a.radius;tmbbox[BOXRIGHT]=tmx+a.radius;
        tmbbox[BOXBOTTOM]=tmy-a.radius;tmbbox[BOXTOP]=tmy+a.radius;
        if(!PIT_CheckLine(&line))I_Error("Spawn wall escape failed");
        tmx=13*FRACUNIT;if(PIT_CheckLine(&line))I_Error("Spawn wall escape moved into wall");
        tmx=-19*FRACUNIT;if(PIT_CheckLine(&line))I_Error("Spawn wall escape crossed wall");
        a.x=32*FRACUNIT;tmx=29*FRACUNIT;if(PIT_CheckLine(&line))I_Error("Spawn wall escape allowed new overlap");
    }
    {
        extern boolean P_RetreatFromDropoff(mobj_t*,fixed_t,fixed_t);
        extern fixed_t tmbbox[4],tmfloorz;
        fixed_t savedFloor=tmfloorz;
        sector_t high={0},low={0};subsector_t sub={0};
        vertex_t v1={0,0},v2={0,100*FRACUNIT};line_t line={0};line_t *edges[]={&line};
        high.linecount=1;high.lines=edges;low.floorheight=-104*FRACUNIT;
        line.v1=&v1;line.v2=&v2;line.dy=100*FRACUNIT;line.slopetype=ST_VERTICAL;
        line.frontsector=&high;line.backsector=&low;sub.sector=&high;a.subsector=&sub;
        a.x=24*FRACUNIT;a.y=50*FRACUNIT;a.z=a.floorz=tmfloorz=0;
        tmbbox[BOXLEFT]=-3*FRACUNIT;tmbbox[BOXRIGHT]=57*FRACUNIT;
        tmbbox[BOXBOTTOM]=20*FRACUNIT;tmbbox[BOXTOP]=80*FRACUNIT;
        if(!P_RetreatFromDropoff(&a,27*FRACUNIT,a.y))I_Error("Lowered lift retreat rejected");
        if(P_RetreatFromDropoff(&a,21*FRACUNIT,a.y))I_Error("Retreat allowed approach to cliff");
        if(P_RetreatFromDropoff(&a,-3*FRACUNIT,a.y))I_Error("Retreat allowed falling off cliff");
        a.x=32*FRACUNIT;if(P_RetreatFromDropoff(&a,27*FRACUNIT,a.y))I_Error("Retreat allowed new ledge overlap");
        a.x=24*FRACUNIT;demoplayback=true;
        if(P_RetreatFromDropoff(&a,27*FRACUNIT,a.y))I_Error("Retreat changed demo behavior");
        demoplayback=false;tmfloorz=savedFloor;
        puts("Lowered lift: safe retreat, cliff avoidance and demo compatibility passed.");
    }
    tmthing=previous;
    puts("Monster overlap: separation, deeper/new collisions and demo compatibility passed.");
}
static void test_fluid_monsters(void){
    if(!P_FluidEnabled())return;
    mobjtype_t types[]={MT_POSSESSED,MT_SHOTGUY,MT_CHAINGUY,MT_WOLFSS,MT_TROOP,MT_SERGEANT,MT_SHADOWS,MT_BRUISER,MT_KNIGHT,MT_UNDEAD,MT_FATSO,MT_VILE,MT_BABY,MT_CYBORG,MT_SPIDER};
    mobj_t *player=players[0].mo;
    int original=states[S_POSS_RUN1].tics;
    for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);++i)for(int duration=2;duration<=4;++duration){
        mobj_t *actor=P_SpawnMobj(player->x-256*FRACUNIT,player->y,ONFLOORZ,types[i]);
        actor->flags|=MF_NOCLIP;actor->target=player;actor->reactiontime=999;
        actor->threshold=100;actor->movecount=10;actor->movedir=0;actor->angle=0;
        fixed_t start=actor->x;
        states[S_POSS_RUN1].tics=duration;
        P_SetMobjState(actor,S_POSS_RUN1);
        fixed_t step=(fixed_t)((int64_t)actor->info->speed*FRACUNIT/duration);
        if(actor->x-start!=step)I_Error("Fluid initial step type %d duration %d",types[i],duration);
        for(int t=1;t<duration;++t)P_FluidTick(actor);
        if(actor->x-start!=step*duration||actor->movecount!=9||actor->reactiontime!=998)
            I_Error("Fluid speed or AI cadence type %d duration %d",types[i],duration);
        fixed_t end=actor->x;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid extra substep");
        P_SetMobjState(actor,S_POSS_RUN1);actor->movedir=8;end=actor->x;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid direction cancellation");
        actor->movedir=0;P_SetMobjState(actor,S_POSS_RUN1);
        P_UnsetThingPosition(actor);actor->x+=48*FRACUNIT;actor->y+=48*FRACUNIT;P_SetThingPosition(actor);end=actor->x;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid diagonal teleport cancellation");
        actor->movedir=0;P_SetMobjState(actor,S_POSS_RUN1);end=actor->x;
        mobj_t *blocker=P_SpawnMobj(actor->x,actor->y,actor->z,MT_POSSESSED);
        actor->flags&=~MF_NOCLIP;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid collision bypass");
        P_RemoveMobj(blocker);actor->flags|=MF_NOCLIP;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid blocked sequence was not cancelled");
        P_SetMobjState(actor,S_POSS_RUN1);end=actor->x;actor->health=0;P_FluidTick(actor);
        if(actor->x!=end)I_Error("Fluid death cancellation");
        P_RemoveMobj(actor);
    }
    states[S_POSS_RUN1].tics=original;
    {
        mobj_t *actor=P_SpawnMobj(player->x-512*FRACUNIT,player->y,ONFLOORZ,MT_POSSESSED);
        actor->flags|=MF_NOCLIP;actor->target=player;actor->reactiontime=999;
        actor->threshold=100;actor->movecount=100;actor->movedir=0;actor->angle=0;
        P_SetMobjState(actor,S_POSS_RUN1);
        for(int tic=0;tic<24;++tic){
            fixed_t x=actor->x;
            if(R_BeforeTic)R_BeforeTic();
            P_MobjThinker(actor);
            if(actor->x-x!=actor->info->speed*FRACUNIT/4)I_Error("Fluid walking paused or jumped on tic %d",tic);
#ifdef DOOM_RENDER3D
            {extern void I_TestSmoothActor(mobj_t*);I_TestSmoothActor(actor);}
#endif
        }
        if(actor->reactiontime!=992)I_Error("Fluid AI cadence changed across walking states");
        P_RemoveMobj(actor);
        puts("FluidMonsters: continuous per-tic walking across six live AI state transitions passed.");
    }
    puts("FluidMonsters: 15 monster types, fractional speed, AI cadence and cancellation passed.");
}
static int observe_sound(int s,int v,int sep,int pitch,int priority,int weapon){++routed_sounds;return -1;}
static void test_map_sound_origins(void){
    int (*saved)(int,int,int,int,int,int)=S_WeaponSound;
    mobj_t fixture;
    mobj_t *player=players[0].mo;
    memset(&fixture,0,sizeof(fixture));
    fixture.x=player->x;fixture.y=player->y;fixture.z=player->z;
    /* A positional source is not an actor. Poison bytes past its prefix to
     * make accidental reads of player/target fail deterministically. */
    fixture.player=(player_t *)(size_t)1;
    fixture.target=(mobj_t *)(size_t)1;
    S_WeaponSound=observe_sound;routed_sounds=0;
    S_StartSound(&fixture,sfx_doropn);S_StopSound(&fixture);
    S_StartSound(&player->subsector->sector->soundorg,sfx_pstart);
    S_StopSound(&player->subsector->sector->soundorg);
    if(routed_sounds)I_Error("Map sound incorrectly routed as a weapon");
    S_StartSound(player,sfx_pistol);S_StopSound(player);
    if(routed_sounds!=1)I_Error("Player weapon sound routing lost");
    {
        mobj_t *rocket=P_SpawnMobj(player->x,player->y,player->z,MT_ROCKET);
        rocket->target=player;
        S_StartSound(rocket,sfx_barexp);S_StopSound(rocket);
        if(routed_sounds!=2)I_Error("Projectile weapon sound routing lost");
        P_RemoveMobj(rocket);
    }
    S_WeaponSound=saved;
    puts("Map/door sound origins and player/projectile routing passed.");
}
#ifdef DOOM_RENDER3D
static void test_settings(void){
    static int phase,frames,was_fullscreen;static SDL_Window *test_window;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing)return;
    if(!test_window){int count=0;SDL_Window **windows=SDL_GetWindows(&count);if(count)test_window=windows[0];SDL_free(windows);if(!test_window)return;was_fullscreen=(SDL_GetWindowFlags(test_window)&SDL_WINDOW_FULLSCREEN)!=0;}
    if(phase==0){
        if(++frames==1)I_Render3DSettingsMenu();
        if(frames==3)I_Render3DScreenshot();
        if(frames==8){I_Render3DSettingsKey(SDL_SCANCODE_RIGHT,0);I_Render3DSettingsKey(SDL_SCANCODE_DOWN,0);I_Render3DSettingsKey(SDL_SCANCODE_RETURN,0);phase=1;}
    }else if(phase==1&&gamemap==2){
        I_Render3DSettingsMenu();I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);
        I_Render3DSettingsKey(SDL_SCANCODE_RETURN,0);I_Render3DSettingsKey(SDL_SCANCODE_ESCAPE,0);SDL_SyncWindow(test_window);phase=2;
    }else if(phase==2){
        if(((SDL_GetWindowFlags(test_window)&SDL_WINDOW_FULLSCREEN)!=0)==was_fullscreen)I_Error("Fullscreen setting did not apply");
        I_Render3DSettingsMenu();I_Render3DSettingsKey(SDL_SCANCODE_RETURN,0);I_Render3DSettingsKey(SDL_SCANCODE_ESCAPE,0);SDL_SyncWindow(test_window);phase=3;
    }else if(phase==3){
        if(((SDL_GetWindowFlags(test_window)&SDL_WINDOW_FULLSCREEN)!=0)!=was_fullscreen)I_Error("Fullscreen setting did not restore");
        puts("F4 map selection and fullscreen on/off passed.");I_Quit();
    }
}
static void test_wheel(void){
    static int frame;
    if(gamestate!=GS_LEVEL||gameaction!=ga_nothing)return;
    player_t *p=&players[0];
    if(++frame==1){
        for(int slot=0;slot<NUMWEAPONS;++slot)p->weaponowned[slot]=true;
        for(int slot=0;slot<NUMWEAPONS;++slot){p->pendingweapon=wp_nochange;float angle=slot*6.283185307f/9-1.570796327f;I_Render3DWheelKey(1);I_Render3DWheelMove(cosf(angle)*180,sinf(angle)*150);I_Render3DWheelKey(0);if(p->pendingweapon!=(slot==p->readyweapon?wp_nochange:slot))I_Error("Wheel wrong slot %d",slot);}
        p->weaponowned[wp_bfg]=false;p->pendingweapon=wp_nochange;
        I_Render3DWheelKey(1);I_Render3DWheelMove(cosf(6*6.283185307f/9-1.570796327f)*180,sinf(6*6.283185307f/9-1.570796327f)*150);I_Render3DWheelKey(0);
        if(p->pendingweapon!=wp_nochange)I_Error("Wheel granted unowned weapon");
        I_Render3DWheelKey(1);I_Render3DWheelKey(-1);if(I_Render3DWheelOpen()||p->pendingweapon!=wp_nochange)I_Error("Wheel cancel failed");
        p->readyweapon=wp_chainsaw;p->cmd.buttons=BT_CHANGE|(wp_fist<<BT_WEAPONSHIFT);P_PlayerThink(p);p->cmd.buttons=0;
        if(p->pendingweapon!=wp_fist)I_Error("Chainsaw prevented fist selection");
        p->pendingweapon=wp_nochange;I_Render3DWheelKey(1);I_Render3DWheelMove(90,-90);
    }
    if(frame==3)I_Render3DScreenshot();
    if(frame==8){I_Render3DWheelKey(-1);puts("Weapon wheel: nine slots, unowned rejection, cancel and chainsaw/fist toggle passed.");I_Quit();}
}
#endif
static void test_sigil_route(void){
    static int route,finale_frames;
    if(gameaction!=ga_nothing)return;
    if(route==0&&gamestate==GS_LEVEL){
        const mapinfo_t *info=G_MapInfo(5,1);
        if(gameepisode!=5||gamemap!=1||!info||strcmp(info->episodepic,"M_EPI5"))I_Error("SIGIL episode metadata missing");
        G_DeferedInitNew(sk_medium,5,6);route=1;
    }else if(route==1&&gamestate==GS_LEVEL&&gamemap==6){G_SecretExitLevel();route=2;}
    else if(route==2&&gamestate==GS_INTERMISSION){
        if(wminfo.next!=8)I_Error("SIGIL secret exit must lead to E5M9");G_WorldDone();route=3;
    }else if(route==3&&gamestate==GS_LEVEL&&gamemap==9){G_ExitLevel();route=4;}
    else if(route==4&&gamestate==GS_INTERMISSION){
        if(wminfo.next!=6)I_Error("SIGIL secret map must return to E5M7");G_WorldDone();route=5;
    }else if(route==5&&gamestate==GS_LEVEL&&gamemap==7){G_DeferedInitNew(sk_medium,5,8);route=6;}
    else if(route==6&&gamestate==GS_LEVEL&&gamemap==8){G_ExitLevel();route=7;}
    else if(route==7&&gamestate==GS_FINALE&&++finale_frames>10){
        const mapinfo_t *info=G_MapInfo(5,8);
        if(!info||strcmp(info->endpic,"CREDIT")||!strstr(info->intertext,"Baphomet"))I_Error("SIGIL ending missing");
        puts("SIGIL episode, intermission, E5M6 -> E5M9 -> E5M7, and ending passed.");I_Quit();
    }
}
static int player_attacks;
static void (*renderer_attack)(player_t *);
static void observe_attack(player_t *player) {
    ++player_attacks;
    if(renderer_attack)renderer_attack(player);
}
static void (*renderer_flash)(mobj_t *,int,int);
static void observe_flash(mobj_t *source,int weapon,int projectile)
{
    if(source->player) ++player_flashes; else ++enemy_flashes;
#ifdef DOOM_RENDER3D
    if(getenv("DOOM_CAPTURE_FLASH_TEST") &&
       ((source->player && player_flashes==1)||(!source->player && enemy_flashes==1))) I_Render3DScreenshot();
#endif
    if(renderer_flash) renderer_flash(source,weapon,projectile);
}
#ifdef DOOM_RENDER3D
static unsigned serial_before_change;
static int projectile_capture;
#endif
static void key(evtype_t type, int value)
{
    event_t event = {type, value, 0, 0};
    D_PostEvent(&event);
}
void I_TestFrame(void)
{
    int test_tic=gametic-test_start;
#ifdef DOOM_RENDER3D
    if(getenv("DOOM_CAPTURE_IMPACTS")&&gamestate==GS_LEVEL){
        static int start=-1,shot=0,capture=0,menu=0;player_t *p=&players[0];
        if(start<0){
            line_t *face=0;
            for(int i=0;i<numlines;++i){line_t *l=&lines[i];
                if(l->special==1&&l->backsector&&l->backsector->floorheight==l->backsector->ceilingheight){face=l;break;}}
            if(!face)I_Error("No impact capture wall");
            double dx=(double)face->dx/FRACUNIT,dy=(double)face->dy/FRACUNIT,len=sqrt(dx*dx+dy*dy);
            fixed_t x=(fixed_t)(((double)face->v1->x+face->v2->x)/2+dy/len*96*FRACUNIT);
            fixed_t y=(fixed_t)(((double)face->v1->y+face->v2->y)/2-dx/len*96*FRACUNIT);
            P_TeleportMove(p->mo,x,y);p->mo->z=p->mo->floorz;p->viewz=p->mo->z+VIEWHEIGHT;
            p->mo->angle=R_PointToAngle2(x,y,(face->v1->x+face->v2->x)/2,(face->v1->y+face->v2->y)/2);
            p->cheats|=CF_GODMODE;start=gametic;R_SetViewSize(11,0);
        }
        int t=gametic-start;
        if(shot<3&&t>=10+shot*15){P_LineAttack(p->mo,p->mo->angle,MISSILERANGE,0,5);++shot;}
        if(shot==3&&t>=65){P_SpawnPlayerMissile(p->mo,MT_ROCKET);++shot;}
        int extended=getenv("DOOM_CAPTURE_FLUID_PROJECTILES")!=NULL;
        if(extended&&shot==4&&t>=105){P_SpawnPlayerMissile(p->mo,MT_PLASMA);++shot;}
        if(extended&&shot==5&&t>=145){P_SpawnPlayerMissile(p->mo,MT_BFG);++shot;}
        const int captures[]={12,27,42,72,76,83,100,112,130,155,180,210};
        if(capture<(extended?12:7)&&t>=captures[capture]){I_Render3DScreenshot();++capture;}
        if(!extended&&t>=95&&!menu){I_Render3DSettingsMenu();for(int i=0;i<2;++i)I_Render3DSettingsKey(SDL_SCANCODE_TAB,0);menu=1;}
        if(t>(extended?225:115))I_Quit();return;
    }
    if(getenv("DOOM_CAPTURE_DOORS")&&gamestate==GS_LEVEL){
        static int start=-1,last=-1,closed=0,shots=0;static line_t *face;static fixed_t x,y;
        player_t *p=&players[0];
        if(start<0){
            for(int i=0;i<numlines;++i){line_t *l=&lines[i];sector_t *sec=l->backsector;
                if(l->special==1&&sec&&!sec->tag&&!sec->special&&!sec->specialdata&&sec->floorheight==sec->ceilingheight){face=l;break;}}
            if(!face)I_Error("No door for capture");
            double dx=(double)face->dx/FRACUNIT,dy=(double)face->dy/FRACUNIT,len=sqrt(dx*dx+dy*dy);
            x=(fixed_t)(((double)face->v1->x+face->v2->x)/2+dy/len*96*FRACUNIT);
            y=(fixed_t)(((double)face->v1->y+face->v2->y)/2-dx/len*96*FRACUNIT);
            P_TeleportMove(p->mo,x,y);p->mo->z=p->mo->floorz;p->viewz=p->mo->z+VIEWHEIGHT;
            p->mo->angle=R_PointToAngle2(x,y,(face->v1->x+face->v2->x)/2,(face->v1->y+face->v2->y)/2);
            p->cheats|=CF_GODMODE;start=gametic;R_SetViewSize(11,0);EV_VerticalDoor(face,p->mo);
        }
        int t=gametic-start;if(last==t)return;last=t;

        if(t==8||t==12||t==22||t==30||t==40||t==65||t==163||t==175){fprintf(stderr,"Door capture tic %d height %d thinker %p hits %d\n",t,face->backsector->ceilingheight/FRACUNIT,face->backsector->specialdata,face->backsector->rocketdoorhits);I_Render3DScreenshot();}
        if(t>=75&&!closed){EV_VerticalDoor(face,p->mo);closed=1;}
        if(shots<3&&t>=150+10*shots){
            ++shots;
            mobj_t rocket={0};rocket.type=MT_ROCKET;rocket.target=p->mo;rocket.x=x;rocket.y=y;
            rocket.z=face->backsector->floorheight+48*FRACUNIT;rocket.height=8*FRACUNIT;
            p_destructibledoors=1;P_RocketDoorImpact(&rocket,face);
        }
        if(t>190)I_Quit();return;
    }
    if(getenv("DOOM_CAPTURE_SSG_FIDGET")) {
        static int captured=-1;
        if(test_tic!=captured&&(test_tic==205||test_tic==207||test_tic==210||test_tic==215||test_tic==275||test_tic==280)) {
            captured=test_tic;I_Render3DScreenshot();
        }
    }

    if(getenv("DOOM_CAPTURE_ORIGINAL_SWITCH")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1;int t;player_t *p=&players[0];
        if(begun<0){begun=gametic;p->cheats|=CF_GODMODE;R_SetViewSize(11,0);for(int w=0;w<NUMWEAPONS;++w)p->weaponowned[w]=true;for(int a=0;a<NUMAMMO;++a)p->ammo[a]=200;}
        if(last==gametic)return;last=gametic;t=gametic-begun;
        if(t>=35&&t<665&&(t-35)%70==0)p->pendingweapon=(t-35)/70;
        if(t>=35&&t<665){int phase=(t-35)%70;
            if(phase==7||phase==23||phase==29||phase==36||phase==52){
                printf("Switch capture: requested weapon %d phase %d\n",(t-35)/70,phase);I_Render3DScreenshot();
            }
        }
        if(t==675){puts("Original switch capture sequence passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_SWITCH_CLOAK")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1;int t;player_t *p=&players[0];
        if(begun<0){begun=gametic;p->cheats|=CF_GODMODE;R_SetViewSize(11,0);for(int w=0;w<NUMWEAPONS;++w)p->weaponowned[w]=true;for(int a=0;a<NUMAMMO;++a)p->ammo[a]=200;}
        if(last==gametic)return;last=gametic;t=gametic-begun;
        if(t>=35&&t<350&&(t-35)%35==0)p->pendingweapon=(t-35)/35;
        if(t>=35&&t<350&&((t-35)%35==7||(t-35)%35==23))I_Render3DScreenshot();
        if(t==355){p->powers[pw_invisibility]=500;}
        if(t==360)I_Render3DScreenshot();
        if(t==365)p->powers[pw_invisibility]=8;
        if(t==380){I_Render3DScreenshot();puts("Weapon switch and invisibility capture sequence passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_KEY_LIGHTS")&&gamestate==GS_LEVEL&&gametic>20){
        extern void I_TestKeyLights(void);I_TestKeyLights();I_Quit();return;
    }
    if(getenv("DOOM_TEST_BROKEN_FLICKER")&&gamestate==GS_LEVEL&&gametic>20){
        extern void I_TestBrokenFlicker(void);I_TestBrokenFlicker();I_Quit();return;
    }
    if(getenv("DOOM_TEST_MENU")) {
        static int last=-1,phase=0,start=-1;
        if(start<0)start=gametic;
        if(last==gametic)return;last=gametic;
        int t=gametic-start;
        if(t>=10&&phase==0){I_Render3DScreenshot();phase++;}
        else if(t>=15&&phase==1){key(ev_keydown,KEY_ESCAPE);phase++;}
        else if(t>=20&&phase==2){I_Render3DScreenshot();phase++;}
        else if(t>=25&&phase==3){key(ev_keydown,KEY_DOWNARROW);phase++;}
        else if(t>=30&&phase==4){key(ev_keydown,KEY_ENTER);phase++;}
        else if(t>=35&&phase==5){I_Render3DScreenshot();phase++;}
        else if(t>=40&&phase==6){key(ev_keydown,KEY_BACKSPACE);phase++;}
        else if(t>=45&&phase==7){key(ev_keydown,KEY_UPARROW);phase++;}
        else if(t>=50&&phase==8){key(ev_keydown,KEY_ENTER);phase++;}
        else if(t>=55&&phase==9){I_Render3DScreenshot();phase++;}
        else if(t>=60&&phase==10){key(ev_keydown,KEY_ENTER);phase++;}
        else if(t>=65&&phase==11){key(ev_keydown,KEY_ENTER);phase++;}
        if(phase==12&&gamestate==GS_LEVEL){puts("Title, main menu, options, episode and new-game navigation passed.");I_Quit();}
        if(t>350)I_Error("Startup menu navigation failed phase=%d",phase);
        return;
    }
    if(getenv("DOOM_TEST_GALLERY")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1,phase=0;int t;player_t* p=&players[0];
        if(begun<0){begun=gametic;p->cheats|=CF_GODMODE;R_SetViewSize(11,0);}
        if(last==gametic)return;last=gametic;t=gametic-begun;
        if(t>=10&&phase==0){key(ev_keydown,'w');phase=1;}
        if(t>=25&&phase==1){P_UseLines(p);phase=2;}
        if(t>=80&&phase==2){key(ev_keyup,'w');phase=3;}
        if(t>=90&&phase==3){if(p->mo->subsector->sector-sectors!=2)I_Error("Gallery traversal: sector=%d x=%d y=%d door=%d",(int)(p->mo->subsector->sector-sectors),p->mo->x/FRACUNIT,p->mo->y/FRACUNIT,sectors[3].ceilingheight/FRACUNIT);I_Render3DScreenshot();phase=4;}
        if(t>=110&&phase==4){p->mo->angle+=ANG180;phase=5;}
        if(t>=125&&phase==5){I_Render3DScreenshot();phase=6;}
        if(t>=145){puts("Material gallery loaded; working door and room traversal passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_KEYS")&&gamestate==GS_LEVEL){
        static int begun=-1,last=-1;static mobj_t* cards[3];static unsigned event;
        extern unsigned p_keypickup_serial[MAXPLAYERS];
        extern int I_TestKeyLight(mobj_t*);
        player_t* p=&players[0];int t;
        if(begun<0){
            int i;double angle=p->mo->angle*(6.283185307179586/4294967296.0);begun=gametic;
            p->cheats|=CF_GODMODE;R_SetViewSize(11,0);
            for(i=0;i<3;++i){
                fixed_t x=p->mo->x+(fixed_t)((cos(angle)*96-sin(angle)*(i-1)*48)*FRACUNIT);
                fixed_t y=p->mo->y+(fixed_t)((sin(angle)*96+cos(angle)*(i-1)*48)*FRACUNIT);
                cards[i]=P_SpawnMobj(x,y,ONFLOORZ,(mobjtype_t)(MT_MISC4+i));
            }
            p->cards[it_redcard]=0;event=p_keypickup_serial[0];
        }
        t=gametic-begun;
        if(last==gametic)return;last=gametic;
        if(t==35)I_Render3DScreenshot();
        if(t==50){int i;for(i=0;i<3;++i)if(!I_TestKeyLight(cards[i]))I_Error("Keycard light missing");}
        if(t==60){cards[1]->z=p->mo->z;P_TouchSpecialThing(cards[1],p->mo);if(!p->cards[it_redcard]||p_keypickup_serial[0]!=event+1)I_Error("Key pickup notification missing");P_GiveCard(p,it_redcard);if(p_keypickup_serial[0]!=event+1)I_Error("Duplicate key notification");}
        if(t==70&&I_TestKeyLight(cards[1]))I_Error("Collected key left a light behind");
        if(t==64||t==80||t==110||t==165||t==180)I_Render3DScreenshot();
        if(t==135){int i,mapped=0;for(i=0;i<numlines;++i)if(lines[i].flags&ML_MAPPED)++mapped;if(mapped==0||mapped==numlines)I_Error("Automap visibility discovery invalid");key(ev_keydown,KEY_TAB);}
        if(t==136)key(ev_keyup,KEY_TAB);
        if(t==145){if(!automapactive)I_Error("Automap did not open");I_Render3DScreenshot();}
        if(t==155)key(ev_keydown,KEY_TAB);
        if(t==156)key(ev_keyup,KEY_TAB);
        if(t>=190){if(automapactive)I_Error("Automap did not close");puts("Keycard lighting, pickup event, duplicate suppression, light removal and automap discovery passed.");I_Quit();}
        return;
    }
    if(getenv("DOOM_TEST_SETTINGS")){test_settings();return;}
    if(getenv("DOOM_TEST_WHEEL")){test_wheel();return;}
#endif
    if(getenv("DOOM_TEST_SIGIL_ROUTE")){test_sigil_route();return;}
#ifdef DOOM_RENDER3D
    if(stage==14&&test_tic>=475){
        if(gamestate!=GS_INTERMISSION)I_Error("Exit notice fixture did not enter intermission");
        if(test_tic==475)I_Render3DScreenshot();
        if(test_tic>=480)I_Quit();
    }
#endif
    if (gamestate != GS_LEVEL) return;
    if(P_UseUZMovement() && stage==3){
        if(test_tic==60){
            if(abs(players[0].mo->momx)>FRACUNIT/2||abs(players[0].mo->momy)>FRACUNIT/2)I_Error("UZ movement did not slow after coasting");
            key(ev_keydown,' ');
        }
        if(test_tic==63){
            if(players[0].mo->z<=players[0].mo->floorz||players[0].mo->momz<=0)I_Error("Jump failed to leave floor");
            key(ev_keyup,' ');
        }
        if(test_tic==100){
            if(players[0].mo->z!=players[0].mo->floorz)I_Error("Jump failed to land");
            if(abs(players[0].mo->momx)>FRACUNIT/8||abs(players[0].mo->momy)>FRACUNIT/8)I_Error("UZ movement did not settle after landing");
            puts("UZ movement coast, jump and landing passed.");
        }
    }
#ifdef DOOM_RENDER3D
    if(getenv("DOOM_TEST_FIDGETS")&&(test_tic==204||test_tic==218||test_tic==285))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_FIRING_LAYERS")&&(test_tic==383||test_tic==385||test_tic==389||test_tic==416))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_PLASMA_RECOVERY")&&(test_tic==414||test_tic==417||test_tic==421||test_tic==427||test_tic==433))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_HAND_BOUNDS")&&((test_tic>=192&&test_tic<=202&&test_tic%2==0)||(test_tic>=204&&test_tic<=215)))I_Render3DScreenshot();
    if(getenv("DOOM_TEST_LOOKDOWN")&&test_tic==360)I_Render3DScreenshot();
#endif
    if (stage == 0 && test_tic >= 5)
    {
        if(test_episode==5){
            const mapinfo_t *info=G_MapInfo(5,1);
            if(gameepisode!=5||!info||strcmp(info->title,"Baphomet's Demesne")||strcmp(info->sky,"SKY5")||strcmp(info->music,"D_E5M1"))I_Error("SIGIL episode/metadata not applied");
        }
        test_start=gametic;
        test_notices();
        test_map_sound_origins();
        test_fluid_monsters();
        test_overlap_escape();
        test_pursuit_guards();
        test_aggressive_ai();
        test_advanced_tactics();
        test_projectile_pitch();
        test_fast_pistol();
        {
            int weapons=p_fluidweapons,monsters=p_fluidmonsters;
            p_fluidweapons=0;p_fluidmonsters=0;
            if(P_FluidWeaponsEnabled()||P_FluidEnabled())I_Error("Mod disable failed");
            p_fluidweapons=weapons;p_fluidmonsters=monsters;
#ifdef DOOM_RENDER3D
            extern void I_TestBundledModSettings(void);I_TestBundledModSettings();
            {extern void I_TestImpactOptions(void);I_TestImpactOptions();}
#endif
        }
        const char *requested=getenv("DOOM_TEST_WEAPON");
        if(requested) {
            test_weapon=atoi(requested);
            if(test_weapon<wp_fist||test_weapon>=NUMWEAPONS)I_Error("Invalid test weapon");
            test_ammo=weaponinfo[test_weapon].ammo;if(test_ammo==am_noammo)test_ammo=am_clip;
            players[0].weaponowned[test_weapon]=true;
            players[0].readyweapon=test_weapon;
            players[0].ammo[test_ammo]=50;
            P_SetupPsprites(&players[0]);
        }
#ifdef DOOM_RENDER3D
        if (!R_AcceleratedView || !R_MuzzleFlash) I_Error("Metal smoke test: GPU renderer or flash hook is inactive");
        I_Render3DToggleFlashlight(); /* Exercise beam/bounce during movement and firing. */
        I_Render3DLook(-80);
#endif
        renderer_flash=R_MuzzleFlash;R_MuzzleFlash=observe_flash;
        renderer_attack=R_WeaponAttack;R_WeaponAttack=observe_attack;
        key(ev_keydown, 'w'); stage = 1;
    }
    else if (stage == 1 && test_tic >= 20)
    {
#ifdef DOOM_RENDER3D
        if(getenv("DOOM_CAPTURE_FLASH_TEST")) I_Render3DScreenshot();
#endif
        key(ev_keyup, 'w');
        /* A map may put a weapon pickup in the walking fixture (SIGIL does).
         * Test the requested weapon, not the pickup's automatic switch. */
        players[0].readyweapon=test_weapon;players[0].pendingweapon=wp_nochange;
        P_SetPsprite(&players[0],ps_weapon,weaponinfo[test_weapon].readystate);
        key(ev_keydown, KEY_RCTRL); stage = 2;
    }
    else if (stage == 2 && test_tic >= 35)
    {
        mobj_t *player=players[0].mo;
        if (!P_UseUZMovement() && (abs(player->momx) > FRACUNIT/8 || abs(player->momy) > FRACUNIT/8))
            I_Error("Smoke test: player still drifts after releasing movement");
        mobj_t *shooter=P_SpawnMobj(player->x,player->y,ONFLOORZ,MT_POSSESSED);
        key(ev_keyup, KEY_RCTRL);
        shooter->target=player;
        A_PosAttack(shooter);
        P_RemoveMobj(shooter);
        if(enemy_flashes!=1) I_Error("Smoke test: enemy shot did not notify flash renderer");
#ifdef DOOM_RENDER3D
        /* Keep the projectile capture clear of the fixture shot's red pain tint. */
        players[0].damagecount=0;
        if(!getenv("DOOM_TEST_FIDGETS"))P_SpawnPlayerMissile(player,MT_BRUISERSHOT);
#endif
        stage = 3;
    }
#ifdef DOOM_RENDER3D
    else if (stage == 3 && test_tic >= 45 && !projectile_capture)
    {
        if(getenv("DOOM_CAPTURE_FLASH_TEST")) I_Render3DScreenshot();
        projectile_capture=1;
    }
#endif
    else if (stage == 3 && test_tic >= 140)
    {
#ifdef DOOM_RENDER3D
        I_Render3DToggleFlashlight(); /* Exercise the reflected-color fade-out. */
        I_Render3DLook(80);
#endif
        saved_ammo = players[0].ammo[test_ammo];
        saved_health = players[0].health;
        if(!player_attacks)I_Error("Smoke test: weapon attack never ran (weapon %d, pending %d, health %d, state %d, paused %d, menu %d, tick %d)",players[0].readyweapon,players[0].pendingweapon,players[0].health,players[0].psprites[0].state?(int)(players[0].psprites[0].state-states):-1,paused,menuactive,test_tic);
        if (test_weapon!=wp_fist&&test_weapon!=wp_chainsaw&&player_flashes == 0) I_Error("Smoke test: player shots did not notify flash renderer");
        if(M_CheckParm("-fluid-sounds") && !I_NamedSoundStarts()) I_Error("Smoke test: named Fluid Weapons sounds never started");
        if (test_weapon!=wp_fist&&test_weapon!=wp_chainsaw&&saved_ammo >= 50) I_Error("Smoke test: firing did not consume ammo");
        {
            mobj_t *reference=P_SpawnMobj(players[0].mo->x,players[0].mo->y,
                players[0].mo->z+100*FRACUNIT,MT_TROOP);
            reference->flags=MF_NOGRAVITY|MF_NOCLIP;
            reference->health=123456;reference->tics=-1;
            reference->target=players[0].mo;reference->tracer=players[0].mo;
        }
        P_NoticeWeapon(&players[0],wp_bfg);notice_count(N_WEAPON);
        sectors[0].rocketdoorhits=2;
        G_SaveGame(5, "Native smoke test"); stage = 4;
    }
    else if (stage == 4 && test_tic >= 150 && gameaction == ga_nothing)
    {
        FILE *file = fopen("doomsav5.dsg", "rb");
        if (!file) I_Error("Smoke test: save file was not created");
        fclose(file);
        players[0].ammo[test_ammo] = 1;
        sectors[0].rocketdoorhits=0;
        G_LoadGame("doomsav5.dsg"); stage = 5;
    }
    else if (stage == 5 && test_tic >= 165 && gameaction == ga_nothing)
    {
        if (players[0].ammo[test_ammo] != saved_ammo || players[0].health != saved_health)
            I_Error("Smoke test: saved player state was not restored");
        if(sectors[0].rocketdoorhits!=2)I_Error("Door damage lost through actual save/load");
        if (!players[0].mo) I_Error("Smoke test: player object was not restored");
        {
            thinker_t *thinker;int found=0;
            for(thinker=thinkercap.next;thinker!=&thinkercap;thinker=thinker->next)
                if(thinker->function.acp1==(actionf_p1)P_MobjThinker) {
                    mobj_t *actor=(mobj_t*)thinker;
                    if(actor->health==123456){
                        if(actor->target!=players[0].mo||actor->tracer!=players[0].mo)
                            I_Error("Smoke test: saved actor target/tracer references were lost");
                        ++found;P_RemoveMobj(actor);
                    }
                }
            if(found!=1)I_Error("Smoke test: actor reference fixture missing");
        }
        P_NoticeWeapon(&players[0],wp_bfg);
        if(notice_count(N_WEAPON))I_Error("Weapon discovery replayed after actual save/load");
        puts("Native gameplay, firing, save, and load passed.");
#ifdef DOOM_RENDER3D
        if(getenv("DOOM_TEST_FIDGETS")){
            R_SetViewSize(11,0);
            players[0].ammo[test_ammo]=200;
            players[0].readyweapon=test_weapon;players[0].pendingweapon=wp_nochange;
            P_SetupPsprites(&players[0]);stage=7;return;
        }
        I_Render3DToggleFlashlight(); /* Keep it on through load and level reset. */
        serial_before_change = r_levelserial;
        G_DeferedInitNew(sk_medium, test_episode, 2);
        stage = 6;
#else
        I_Quit();
#endif
    }
#ifdef DOOM_RENDER3D
    else if(stage==7&&test_tic>=190){
        if(getenv("DOOM_TEST_LOOKDOWN"))I_Render3DLook(10000);
        event_t aux={ev_mouse,2,0,0};players[0].ammo[test_ammo]=200;D_PostEvent(&aux);stage=8;
    }
    else if(stage==8&&test_tic>=235){
        event_t aux={ev_mouse,0,0,0};D_PostEvent(&aux);I_Render3DScreenshot();stage=9;
    }
    else if(stage==9&&test_tic>=260){key(ev_keydown,'r');stage=10;}
    else if(stage==10&&test_tic>=300){key(ev_keyup,'r');I_Render3DScreenshot();stage=11;}
    else if(stage==11&&test_tic>=380){
        if(players[0].ammo[test_ammo]!=200)I_Error("Fidget controls consumed ammo");
        key(ev_keydown,KEY_RCTRL);stage=12;
    }
    else if(stage==12&&test_tic>=410){key(ev_keyup,KEY_RCTRL);stage=13;}
    else if(stage==13&&test_tic>=450){
        if(test_weapon!=wp_fist&&test_weapon!=wp_chainsaw&&players[0].ammo[test_ammo]>=200)I_Error("Fidget blocked subsequent firing");
        puts("Right-click and R fidgets preserved ammo; subsequent firing passed.");
        if(getenv("DOOM_TEST_EXIT_NOTICES")){
            P_NoticeLevel();players[0].killcount=totalkills;players[0].secretcount=totalsecret;players[0].itemcount=totalitems;
            G_SecretExitLevel();stage=14;return;
        }
        serial_before_change=r_levelserial;G_DeferedInitNew(sk_medium,1,2);stage=6;
    }
    else if (stage == 6 && test_tic >= 190 && gameaction == ga_nothing)
    {
        if (gamemap != 2 || r_levelserial <= serial_before_change || !players[0].mo)
            I_Error("Metal smoke test: level change failed");
        puts("Metal level change passed.");
        I_Quit();
    }
#endif
}
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 40) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    FILE *config = fopen("smoke.cfg", "w");
    if (!config) return 1;
    fputs("key_up 119\nkey_fire 157\nsnd_channels 16\n", config);
    if(getenv("DOOM_TEST_FIDGETS"))fputs("screenblocks 11\n",config);
    fclose(config);
    char *args[64] = {"smoke", "-iwad", argv[1], "-warp", "1", "1",
        "-config", "smoke.cfg", "-nomonsters", "-nomusic"};
    int count=10;for(int i=2;i<argc;++i)args[count++]=argv[i];
    myargc=count; myargv = args;
    if(getenv("DOOM_TEST_MUSIC"))args[9]="-opl";
    if(getenv("DOOM_TEST_EPISODE")){test_episode=atoi(getenv("DOOM_TEST_EPISODE"));args[4]=getenv("DOOM_TEST_EPISODE");}
    if(getenv("DOOM_TEST_MAP"))args[5]=getenv("DOOM_TEST_MAP");
    if(getenv("DOOM_TEST_MENU")){args[3]="-nomusic";args[4]="-nomusic";args[5]="-nomusic";}
    D_DoomMain();
    return 0;
}



