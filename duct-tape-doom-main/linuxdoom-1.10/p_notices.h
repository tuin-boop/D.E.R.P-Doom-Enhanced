#ifndef P_NOTICES_H
#define P_NOTICES_H
#include "d_player.h"
#define NOTICE_SAVE_BYTES (8+MAXPLAYERS*32)
extern int p_noticecombat;
typedef enum {
 N_CYBER,N_SPIDER,N_SECRET,N_SOUL,N_MEGA,N_INVUL,N_BERSERK,N_INVIS,N_SUIT,N_MAP,N_VISOR,
 N_WEAPON,N_CLOSE,N_CHAIN,N_MULTI,N_FISTS,N_PERFECT,N_EXIT,N_KILLS,N_SECRETS,N_ITEMS,N_ALL,N_COUNT
} noticekind_t;
typedef struct {int kind,detail,variant;} noticeevent_t;
void P_Notice(int player,int kind,int detail);
int P_PopNotice(int player,noticeevent_t* event);
void P_NoticeNewGame(void);
void P_NoticeLevel(void);
void P_NoticeWeapon(player_t* player,int weapon);
void P_NoticeDamage(player_t* player,int damage,mobj_t* source);
void P_NoticeKill(mobj_t* source,mobj_t* target,mobj_t* inflictor);
void P_NoticeTick(player_t* player);
void P_NoticeExit(int secret);
int P_SaveNotices(unsigned char* data);
void P_LoadNotices(const unsigned char* data,int length);
#endif
