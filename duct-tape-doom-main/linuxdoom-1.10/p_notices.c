#include "doomstat.h"
#include "p_local.h"
#include "p_notices.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct {
 unsigned weapons;
 int damaged,low,lastDamage,cooldown[4];
 int groupTic,groupKills,chainTic,chainKills;
 mobj_t* group;
} noticeprogress_t;
static noticeprogress_t progress[MAXPLAYERS];
int p_noticecombat=1;
static noticeevent_t events[MAXPLAYERS][64];
static unsigned head[MAXPLAYERS],tail[MAXPLAYERS],variant[N_COUNT];
void P_Notice(int player,int kind,int detail){
 if(player<0||player>=MAXPLAYERS||kind<0||kind>=N_COUNT)return;
 if(!p_noticecombat&&kind>=N_CLOSE&&kind<=N_PERFECT)return;
 if(head[player]-tail[player]>=64)++tail[player];
 events[player][head[player]++%64]=(noticeevent_t){kind,detail,(int)(variant[kind]++%3)};
}
int P_PopNotice(int player,noticeevent_t* event){
 if(player<0||player>=MAXPLAYERS||tail[player]==head[player])return 0;
 *event=events[player][tail[player]++%64];return 1;
}
void P_NoticeNewGame(void){memset(progress,0,sizeof(progress));memset(head,0,sizeof(head));memset(tail,0,sizeof(tail));}
void P_NoticeLevel(void){
 for(int i=0;i<MAXPLAYERS;++i){unsigned weapons=progress[i].weapons;
  memset(&progress[i],0,sizeof(progress[i]));progress[i].weapons=weapons;
  for(int w=0;w<NUMWEAPONS;++w)if(players[i].weaponowned[w])progress[i].weapons|=1u<<w;
  progress[i].groupTic=progress[i].chainTic=-1;head[i]=tail[i]=0;
 }
}
void P_NoticeWeapon(player_t* player,int weapon){
 int i=(int)(player-players);unsigned bit=1u<<weapon;
 if(!(progress[i].weapons&bit)){progress[i].weapons|=bit;P_Notice(i,N_WEAPON,weapon);}
}
void P_NoticeDamage(player_t* player,int damage){
 noticeprogress_t* p=&progress[player-players];
 if(damage<=0)return;p->damaged=1;p->lastDamage=leveltime;
 if(player->health>0&&player->health<10)p->low=1;
 if(player->health<=0)p->low=0;
}
static void award(int i,int slot,int kind){
 if(leveltime>=progress[i].cooldown[slot]){P_Notice(i,kind,0);progress[i].cooldown[slot]=leveltime+35*20;}
}
void P_NoticeKill(mobj_t* source,mobj_t* target,mobj_t* inflictor){
 if(!(target->info->flags&MF_COUNTKILL)||!source||!source->player)return;
 int i=(int)(source->player-players);noticeprogress_t* p=&progress[i];
 if(target->type==MT_CYBORG)P_Notice(i,N_CYBER,0);
 if(target->type==MT_SPIDER)P_Notice(i,N_SPIDER,0);
 if(inflictor==source&&source->player->readyweapon==wp_fist&&target->info->spawnhealth>=150)award(i,3,N_FISTS);
 // A hitscan volley/radius blast is synchronous. Group by its inflictor
 // and tic, not by a time window that could merge unrelated attacks.
 if(p->groupTic!=leveltime||p->group!=inflictor){p->groupTic=leveltime;p->group=inflictor;p->groupKills=0;}
 if(++p->groupKills==3)award(i,2,N_MULTI);
 if(inflictor&&inflictor->type==MT_BARREL){
  if(p->chainTic!=leveltime){p->chainTic=leveltime;p->chainKills=0;}
  if(++p->chainKills==3)award(i,1,N_CHAIN);
 }
}
void P_NoticeTick(player_t* player){
 int i=(int)(player-players);noticeprogress_t* p=&progress[i];
 if(!p->low||player->health<=0||leveltime-p->lastDamage<35*5||leveltime%35)return;
 for(thinker_t* th=thinkercap.next;th!=&thinkercap;th=th->next){
  if(th->function.acp1!=(actionf_p1)P_MobjThinker)continue;
  mobj_t* mo=(mobj_t*)th;
  if(mo->health>0&&(mo->flags&MF_COUNTKILL)&&mo->target==player->mo&&
     P_AproxDistance(mo->x-player->mo->x,mo->y-player->mo->y)<1024*FRACUNIT&&P_CheckSight(mo,player->mo))return;
 }
 p->low=0;award(i,0,N_CLOSE);
}
void P_NoticeExit(int secret){
 int kills=0,items=0,secrets=0;
 for(int i=0;i<MAXPLAYERS;++i)if(playeringame[i]){kills+=players[i].killcount;items+=players[i].itemcount;secrets+=players[i].secretcount;}
 for(int i=0;i<MAXPLAYERS;++i)if(playeringame[i]){
  if(secret)P_Notice(i,N_EXIT,0);
  if(players[i].health>0&&!progress[i].damaged&&totalkills+totalitems+totalsecret>0&&kills>=totalkills&&items>=totalitems&&secrets>=totalsecret)P_Notice(i,N_PERFECT,0);
 }
}
// Optional trailer after the existing save terminator: no player/mobj layout
// changes, and old saves remain readable. Never serialize runtime pointers.
int P_SaveNotices(unsigned char* data){
 memcpy(data,"DNOTICE1",8);int at=8;
 for(int i=0;i<MAXPLAYERS;++i){noticeprogress_t* p=&progress[i];
  uint32_t values[]={p->weapons,p->damaged,p->low,p->lastDamage,p->cooldown[0],p->cooldown[1],p->cooldown[2],p->cooldown[3]};
  for(int v=0;v<8;++v)for(int b=0;b<4;++b)data[at++]=(unsigned char)(values[v]>>(b*8));
 }return at;
}
void P_LoadNotices(const unsigned char* data,int length){
 P_NoticeLevel();
 for(int i=0;i<MAXPLAYERS;++i)progress[i].damaged=1; // Unknown history in legacy saves.
 if(length<8+MAXPLAYERS*32||memcmp(data,"DNOTICE1",8))return;
 int at=8;
 for(int i=0;i<MAXPLAYERS;++i){uint32_t v[8]={0};for(int n=0;n<8;++n)for(int b=0;b<4;++b)v[n]|=(uint32_t)data[at++]<<(b*8);
  noticeprogress_t* p=&progress[i];p->weapons=v[0];p->damaged=v[1]!=0;p->low=v[2]!=0;p->lastDamage=(int)v[3];
  for(int n=0;n<4;++n)p->cooldown[n]=(int)v[4+n];
 }
}
