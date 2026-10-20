#include "doomdef.h"
#include "p_local.h"
#include "../platform/audio/flyby.h"
#include <stdio.h>
#include <stdlib.h>
static void check(int yes){if(!yes){fprintf(stderr,"Near-miss classification failed\n");exit(1);}}
int main(void){
 mobj_t listener={0},shooter={0};float x,y,z,level;
 listener.x=1000*FRACUNIT;
 check(Flyby_Near(&listener,&shooter,NULL,0,20,41,1200,20,41,&x,&y,&z,&level));
 check(level>0&&level<=1&&x>999&&x<1001);
 check(!Flyby_Near(&listener,&shooter,&listener,0,0,41,1200,0,41,&x,&y,&z,&level));
 check(!Flyby_Near(&listener,&shooter,NULL,0,20,41,900,20,41,&x,&y,&z,&level));
 check(!Flyby_Near(&listener,&shooter,NULL,0,100,41,1200,100,41,&x,&y,&z,&level));
 check(!Flyby_Near(&listener,&listener,NULL,0,20,41,1200,20,41,&x,&y,&z,&level));
 puts("Near-miss: audible pass, direct hit, blocked path, distant pass and own-shot guards passed.");return 0;
}
