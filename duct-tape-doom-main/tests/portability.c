/* Regression checks need no copyrighted game data or display server. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "doomdata.h"
#include "d_deh.h"
#include "dstrings.h"
#include "info.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "m_argv.h"
#include "m_cheat.h"
#include "m_misc.h"
#include "m_swap.h"
#include "p_saveg.h"
#include "p_local.h"
#include "r_data.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#include "../platform/flash_lighting.h"
#include "../platform/emissive.h"
#include "../platform/volumetric_fog.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

extern byte *save_p;
extern char *chat_macros[];
extern int mouseSensitivity;
extern void R_InitTextures(void);

_Static_assert(sizeof(wadinfo_t) == 12, "WAD header layout");
_Static_assert(sizeof(filelump_t) == 16, "WAD directory layout");
_Static_assert(sizeof(mapvertex_t) == 4, "Vertex layout");
_Static_assert(sizeof(mapsidedef_t) == 30, "Side layout");
_Static_assert(sizeof(maplinedef_t) == 14, "Line layout");
_Static_assert(sizeof(mapsector_t) == 26, "Sector layout");
_Static_assert(sizeof(mapsubsector_t) == 4, "Subsector layout");
_Static_assert(sizeof(mapseg_t) == 12, "Segment layout");
_Static_assert(sizeof(mapnode_t) == 28, "BSP node layout");
_Static_assert(sizeof(mapthing_t) == 10, "Thing layout");

static void put16(byte *out, uint16_t value)
{
    out[0] = (byte)value;
    out[1] = (byte)(value >> 8);
}
static void put32(byte *out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out[i] = (byte)(value >> (8 * i));
}

static int texture_fixture(void)
{
    /* Three textures expose pointer-array underallocation on 64-bit hosts. */
    byte pnames[12] = {0}, patch[18] = {0}, textures[112] = {0};
    put32(pnames, 1);
    memcpy(pnames + 4, "PATCH", 5);
    put16(patch, 1);
    put16(patch + 2, 1);
    put32(patch + 8, 12);
    patch[13] = 1;
    patch[15] = 42;
    patch[17] = 255;
    put32(textures, 3);
    for (int i = 0; i < 3; ++i)
    {
        byte *texture = textures + 16 + 32 * i;
        put32(textures + 4 + 4 * i, (uint32_t)(16 + 32 * i));
        memcpy(texture, "TEX0", 4);
        texture[3] = (byte)('0' + i);
        put16(texture + 12, 1);
        put16(texture + 14, 1);
        put16(texture + 20, 1);
    }
    const char *names[] = {"PNAMES", "PATCH", "TEXTURE1", "S_START", "S_END"};
    const byte *data[] = {pnames, patch, textures, NULL, NULL};
    const uint32_t sizes[] = {sizeof(pnames), sizeof(patch), sizeof(textures), 0, 0};
    byte header[12] = {'P', 'W', 'A', 'D'}, directory[80] = {0};
    uint32_t position = 12;
    for (int i = 0; i < 5; ++i)
    {
        put32(directory + 16 * i, position);
        put32(directory + 16 * i + 4, sizes[i]);
        memcpy(directory + 16 * i + 8, names[i], strlen(names[i]));
        position += sizes[i];
    }
    put32(header + 4, 5);
    put32(header + 8, position);
    FILE *file = fopen("portability.wad", "wb");
    CHECK(file);
    CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header));
    for (int i = 0; i < 5; ++i)
        if (sizes[i])
            CHECK(fwrite(data[i], 1, sizes[i], file) == sizes[i]);
    CHECK(fwrite(directory, 1, sizeof(directory), file) == sizeof(directory));
    CHECK(fclose(file) == 0);
    char *files[] = {"portability.wad", NULL};
    W_InitMultipleFiles(files);
    CHECK(W_CheckNumForName("patch") == 1);
    CHECK(W_LumpLength(1) == 18);
    R_InitTextures();
    for (int i = 0; i < 3; ++i)
    {
        char name[] = "TEX0";
        name[3] = (char)('0' + i);
        CHECK(R_CheckTextureNumForName(name) == i);
        CHECK(*R_GetColumn(i, 0) == 42);
        int width, height;
        byte indexed[2];
        R_TextureDimensions(i, &width, &height);
        char texture_name[9];
        R_TextureName(i, texture_name);
        CHECK(strcmp(texture_name, name) == 0);
        CHECK(width == 1 && height == 1);
        R_CopyIndexedTexture(i, indexed);
        CHECK(indexed[0] == 42 && indexed[1] == 255);
    }
    Z_CheckHeap();
    return 0;
}

static int configuration(void)
{
    FILE *file = fopen("portability.cfg", "w");
    CHECK(file);
    fputs("mouse_sensitivity 4096\nchatmacro0 \"64-bit string survives\"\n"
          "chatmacro1 123\nmouse_sensitivity \"wrong type\"\n", file);
    CHECK(fclose(file) == 0);
    char *args[] = {"portability", "-config", "portability.cfg"};
    myargc = 3;
    myargv = args;
    M_LoadDefaults();
    CHECK(mouseSensitivity == 4096);
    CHECK(strcmp(chat_macros[0], "64-bit string survives") == 0);
    M_SaveDefaults();
    file = fopen("portability.cfg", "r");
    CHECK(file);
    char content[4096] = {0};
    CHECK(fread(content, 1, sizeof(content) - 1, file) > 0);
    fclose(file);
    CHECK(strstr(content, "mouse_sensitivity\t\t4096"));
    CHECK(strstr(content, "\"64-bit string survives\""));
    M_LoadDefaults();
    CHECK(mouseSensitivity == 4096);
    CHECK(strcmp(chat_macros[0], "64-bit string survives") == 0);
    return 0;
}

static int allocator(void)
{
    void *blocks[33];
    int before = Z_FreeMemory();
    for (int i = 0; i < 33; ++i)
    {
        blocks[i] = Z_Malloc(i + 1, PU_STATIC, NULL);
        CHECK((uintptr_t)blocks[i] % _Alignof(max_align_t) == 0);
        memset(blocks[i], i, (size_t)i + 1);
    }
    Z_CheckHeap();
    for (int i = 0; i < 33; ++i)
        Z_Free(blocks[i]);
    CHECK(Z_FreeMemory() == before);
    void *owner = NULL;
    Z_Malloc(7, PU_CACHE, &owner);
    CHECK(owner);
    Z_ChangeTag2(owner, PU_CACHE);
    Z_Free(owner);
    CHECK(owner == NULL);
    Z_CheckHeap();
    return 0;
}

static int saved_player(void)
{
    _Alignas(max_align_t) byte buffer[sizeof(player_t) + 64] = {0};
    memset(players, 0, sizeof(players));
    memset(playeringame, 0, sizeof(playeringame));
    playeringame[0] = true;
    players[0].health = 73;
    players[0].psprites[0].state = &states[S_PLAY];
    players[0].psprites[0].tics = 12;
    save_p = buffer + 1;
    P_ArchivePlayers();
    byte *end = save_p;
    memset(&players[0], 0, sizeof(players[0]));
    save_p = buffer + 1;
    P_UnArchivePlayers();
    CHECK(save_p == end);
    CHECK(players[0].health == 73);
    CHECK(players[0].psprites[0].state == &states[S_PLAY]);
    CHECK(players[0].psprites[0].tics == 12);
    return 0;
}

static int sprite_glow(void)
{
    byte palette[768]={0};
    byte pixels[24]={1,255,1,255,1,255,1,255,2,255,2,255,2,255,2,255,3,255,3,255,4,0,4,0};
    float color[3];
    palette[3]=8;palette[4]=255;palette[5]=12; /* Green halo. */
    palette[6]=palette[7]=palette[8]=255; /* White core. */
    palette[11]=40; /* Dark blue outline. */
    palette[12]=255; /* Transparent red should never contribute. */
    CHECK(doom_sprite_glow(pixels,12,palette,color)>0);
    CHECK(color[1]==1 && color[0]<0.1f && color[2]<0.1f);
    palette[3]=255;palette[4]=128;palette[5]=8;
    CHECK(doom_sprite_glow(pixels,12,palette,color)>0);
    CHECK(color[0]==1 && color[1]>0.45f && color[1]<0.55f && color[2]<0.1f);
    CHECK(doom_sprite_glow(pixels+8,4,palette,color)==0);
    CHECK(color[0]==1 && color[1]==1 && color[2]==1); /* Neutral fallback. */
    CHECK(doom_sprite_glow(pixels,0,palette,color)==0);
    return 0;
}

/* Culled blockers light every point exactly like the full list. */
static int flash_culling(void)
{
    static doom_light_blocker_t all[600],kept[600];
    uint32_t seed=12345,before=0,after=0,sliced=0;
#define RANDOM() ((seed=seed*1664525u+1013904223u)>>8)/16777216.0f
    for(int scene=0;scene<40;++scene) {
        float yaw=RANDOM()*6.2832f,pitch=(RANDOM()-0.5f)*2.6f;
        doom_flash_t light={{0,0,32,scene%2?900:300},1,0,0,0,{1,1,1,0.85f},{0,0,0,0}};
        if(scene%2) {
            light.direction[0]=cosf(yaw)*cosf(pitch);light.direction[1]=sinf(yaw)*cosf(pitch);
            light.direction[2]=sinf(pitch);light.direction[3]=0.90f;
        }
        uint32_t count=0;
        for(int i=0;i<600;++i) {
            float x=(RANDOM()-0.5f)*2000,y=(RANDOM()-0.5f)*2000,angle=RANDOM()*6.2832f,length=8+RANDOM()*(i%10?64:400);
            doom_light_blocker_t b={{x,y,x+cosf(angle)*length,y+sinf(angle)*length},{1,-1,0,0}};
            int kind=i%4;
            if(kind==1) {b.opening[0]=RANDOM()*64;b.opening[1]=b.opening[0]-RANDOM()*8;} /* Closed. */
            else if(kind>=2) {b.opening[0]=RANDOM()*64-16;b.opening[1]=b.opening[0]+RANDOM()*128;}
            if(doom_flash_segment_distance(0,0,&b)<4) continue; /* The lamp stands in open space. */
            if(doom_flash_blocker_relevant(&light,&b,kind==0)) all[count++]=b;
        }
        memcpy(kept,all,sizeof(all));
        doom_flash_t culled=light;culled.count=doom_flash_cull_hidden(&light,kept,count);light.count=count;
        before+=count;after+=culled.count;
        static uint16_t indices[65535];
        uint32_t table[DOOM_FLASH_SLICES],listed=doom_flash_slice_blockers(&culled,kept,culled.count,table,indices,65535);
        CHECK(listed!=UINT32_MAX);
        sliced+=listed;
        for(int i=0;i<4000;++i) {
            float r=light.position[3]*RANDOM(),a=RANDOM()*6.2832f;
            float x=cosf(a)*r,y=sinf(a)*r,z=32+(RANDOM()-0.5f)*2*light.position[3];
            if(i%2&&light.direction[3]>0) { /* Mostly inside the beam. */
                x=(light.direction[0]+(RANDOM()-0.5f)*0.8f)*r;y=(light.direction[1]+(RANDOM()-0.5f)*0.8f)*r;
                z=32+(light.direction[2]+(RANDOM()-0.5f)*0.8f)*r;
            }
            float full=doom_flash_at(x,y,z,&light,all);
            CHECK(full==doom_flash_at(x,y,z,&culled,kept));
            CHECK(full==doom_flash_at_sliced(x,y,z,&culled,kept,table,indices));
        }
    }
#undef RANDOM
    CHECK(after*2<before); /* Most of a dense scene is hidden. */
    CHECK(sliced<after*DOOM_FLASH_SLICES/8); /* A slice holds a few of them. */
    return 0;
}

static int flash_lighting(void)
{
    doom_flash_t light={{0,0,32,200},1,0,0,0,{1,1,1,0}};
    doom_light_blocker_t wall={{50,-50,50,50},{1,-1,0,0}};
    CHECK(fabsf(doom_flash_at(0,0,32,&light,NULL)-1)<0.001f);
    CHECK(fabsf(doom_flash_at(100,0,32,&light,NULL)-0.5f)<0.001f);
    CHECK(doom_flash_at(201,0,32,&light,NULL)==0);
    light.count=1;
    CHECK(doom_flash_at(100,0,32,&light,&wall)==0); /* Solid wall. */
    CHECK(doom_flash_at(25,0,32,&light,&wall)>0); /* Same side. */
    wall.opening[0]=0;wall.opening[1]=64;
    CHECK(doom_flash_at(100,0,32,&light,&wall)>0); /* Open door. */
    wall.opening[1]=16;
    CHECK(doom_flash_at(100,0,32,&light,&wall)==0); /* Lowered door. */
    wall.opening[1]=64;wall.opening[0]=40;
    CHECK(doom_flash_at(100,0,32,&light,&wall)==0); /* Raised floor. */
    wall.opening[0]=0;
    CHECK(doom_flash_at(100,0,120,&light,&wall)==0); /* Ray hits above portal. */
    CHECK(doom_flash_at(50,0,32,&light,&wall)>0); /* Receiver is the wall. */
    CHECK(doom_flash_at(0,100,32,&light,&wall)>0); /* Parallel ray. */
    light.count=0;light.direction[0]=1;light.direction[3]=0.90f;
    CHECK(fabsf(doom_flash_at(100,0,32,&light,NULL)-0.5f)<0.001f); /* Beam core. */
    CHECK(doom_flash_at(-100,0,32,&light,NULL)==0); /* Behind the lamp. */
    CHECK(doom_flash_at(0,100,32,&light,NULL)==0); /* Outside the cone. */
    CHECK(doom_flash_at(100,30,32,&light,NULL)>0); /* Soft penumbra. */
    CHECK(doom_flash_at(100,30,32,&light,NULL)<doom_flash_at(100,0,32,&light,NULL));
    light.count=1;
    CHECK(doom_flash_at(100,0,32,&light,&wall)>0); /* Open portal in beam. */
    wall.opening[1]=-1;
    CHECK(doom_flash_at(100,0,32,&light,&wall)==0); /* Beam blocked by door. */
    light.count=0;light.direction[0]=0;light.direction[2]=1;
    CHECK(doom_flash_at(0,0,132,&light,NULL)>0); /* Mouse look points up. */
    CHECK(doom_flash_at(100,0,32,&light,NULL)==0);
    CHECK(doom_flash_fade(0,1)==1);
    CHECK(doom_flash_fade(2,0)>doom_flash_fade(2,1));
    CHECK(doom_flash_fade(1,1)==1);
    CHECK(doom_flash_fade(6,1)==0);
    CHECK(doom_flash_fade(100,1)==0);
    {
        doom_flash_t lamp={{0,0,32,200},1,0,0,0,{1,1,1,0}};
        const float wall[3]={-1,0,0},floor[3]={0,0,1};
        CHECK(doom_flash_facing(100,0,32,wall,&lamp)==1); /* Omnidirectional. */
        lamp.color[3]=0.75f;
        CHECK(fabsf(doom_flash_facing(100,0,32,wall,&lamp)-1)<0.001f); /* Faces lamp. */
        CHECK(fabsf(doom_flash_facing(100,0,32,floor,&lamp)-0.25f)<0.001f); /* Edge-on. */
        CHECK(fabsf(doom_flash_facing(-100,0,32,wall,&lamp)-0.25f)<0.001f); /* Faces away. */
        CHECK(fabsf(doom_flash_facing(100,0,-68,floor,&lamp)-(0.25f+0.75f*0.70711f))<0.001f);
        CHECK(doom_flash_facing(0,0,32,floor,&lamp)==1); /* Light on the surface. */
        lamp.color[3]=0.35f; /* Emissive source 4 units off its own wall. */
        CHECK(doom_flash_facing(4,100,32,wall,&lamp)>0.65f);
    }
    {
        /* A panel 4 units in front of a wall at x=-4, facing +x. */
        doom_flash_t panel={{0,0,64,200},1,0,0,0,{1,1,1,0},{0,1,0,0}};
        const float wall[3]={1,0,0},floor[3]={0,0,1};
        CHECK(fabsf(doom_flash_at(100,0,64,&panel,NULL)-0.5f)<0.001f); /* Straight ahead, full. */
        CHECK(doom_flash_at(-10,0,0,&panel,NULL)==0); /* Behind the wall. */
        CHECK(doom_panel_emission(-4,0,-64,1,0,floor)==0); /* Floor at the wall's foot. */
        CHECK(doom_panel_emission(-2,0,-64,1,0,floor)<doom_panel_emission(20,0,-64,1,0,floor)); /* Fades in. */
        CHECK(doom_panel_emission(-4,40,0,1,0,wall)==1); /* Its own wall stays lit. */
        CHECK(doom_panel_emission(-50,0,0,0,0,NULL)==1); /* Not a panel. */
    }
    {
        doom_flash_t lamp={{0,0,32,200},1,0,0,0,{1,1,1,0}};
        doom_light_blocker_t front={{50,50,50,-50},{1,-1,0,0}}; /* Sector side faces the lamp. */
        doom_light_blocker_t back={{50,-50,50,50},{1,-1,0,0}};
        doom_light_blocker_t far={{250,-50,250,50},{1,-1,0,0}};
        doom_light_blocker_t corner={{150,150,300,150},{1,-1,0,0}}; /* Box overlaps, circle misses. */
        doom_light_blocker_t portal={{50,-50,50,50},{-200,300,0,0}};
        CHECK(doom_flash_blocker_relevant(&lamp,&front,1));
        CHECK(!doom_flash_blocker_relevant(&lamp,&back,1));
        CHECK(!doom_flash_blocker_relevant(&lamp,&far,1));
        CHECK(!doom_flash_blocker_relevant(&lamp,&far,0));
        CHECK(!doom_flash_blocker_relevant(&lamp,&corner,0));
        CHECK(!doom_flash_blocker_relevant(&lamp,&portal,0)); /* Opening covers every height. */
        portal.opening[1]=100;
        CHECK(doom_flash_blocker_relevant(&lamp,&portal,0));
        CHECK(doom_flash_blocker_relevant(&lamp,&back,0)); /* Two-sided lines block from both sides. */
    }
    return 0;
}

/* Keep legacy demo input deterministic while tightening ordinary play. */
extern void P_MovePlayer(player_t *player);
static int movement(void)
{
    player_t player = {0};
    mobj_t actor = {0};
    player.mo = &actor;
    player.playerstate = PST_LIVE;
    player.cmd.forwardmove = 25;
    actor.player = &player;
    actor.state = &states[S_PLAY_RUN1];
    classicmovement = true;
    P_MovePlayer(&player);
    fixed_t original = actor.momx;
    CHECK(original > 0);
    classicmovement = false;
    actor.momx = actor.momy = 0;
    P_MovePlayer(&player);
    CHECK(actor.momx > 2 * original);
    for (int mode = 0; mode < 3; ++mode)
    {
        demoplayback = mode == 0;
        demorecording = mode == 1;
        netgame = mode == 2;
        actor.momx = actor.momy = 0;
        P_MovePlayer(&player);
        CHECK(actor.momx == original);
    }
    demoplayback = demorecording = netgame = false;
    actor.z = FRACUNIT;
    actor.momx = actor.momy = 0;
    P_MovePlayer(&player);
    CHECK(actor.momx == 0 && actor.momy == 0);
    return 0;
}

static int emissive_masks(void)
{
    const byte white[]={240,240,240}, metal[]={155,155,155}, dark[]={24,24,24};
    const byte green[]={32,220,48}, orange[]={240,96,16}, blue[]={24,48,240};
    CHECK(doom_emissive_pixel(doom_emissive_kind("STONE2",0),white,255)==0);
    CHECK(doom_emissive_pixel(doom_emissive_kind("LITE3",0),white,255)==255);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_LAMP,metal,255)==0);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_LAMP,dark,255)==0);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_LAMP,white,0)==0);
    CHECK(doom_emissive_pixel(doom_emissive_kind("COMPSTA1",0),green,255)==255);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_PANEL,white,255)==0);
    const byte brown[]={160,96,32};
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_PANEL,brown,255)==0);
    CHECK(doom_emissive_pixel(doom_emissive_kind("LAVA1",1),orange,255)==255);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_LAVA,blue,255)==0);
    CHECK(doom_emissive_pixel(doom_emissive_kind("NUKAGE2",1),green,255)==255);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_SLIME,orange,255)==0);
    CHECK(doom_emissive_kind("COMP01",1)==DOOM_EMISSIVE_NONE);
    CHECK(doom_emissive_kind("TLITE6_1",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_pixel(doom_emissive_kind("AQLITE01",0),white,255)==255);
    CHECK(doom_emissive_pixel(doom_emissive_kind("AQLITE08",0),blue,255)==255);
    CHECK(doom_emissive_pixel(doom_emissive_kind("AQCOMP01",0),green,255)==255);
    CHECK(doom_emissive_kind("AQPANL08",0)==DOOM_EMISSIVE_NONE);
    CHECK(doom_emissive_kind("AQMETL12",0)==DOOM_EMISSIVE_NONE);
    CHECK(doom_emissive_kind("CEIL1_2",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("CEIL1_3",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("CEIL3_3",1)==DOOM_EMISSIVE_NONE);
    CHECK(doom_emissive_kind("FLAT17",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("FLAT23",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("FLAT2",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("FLAT20",1)==DOOM_EMISSIVE_NONE);
    CHECK(doom_emissive_kind("GRNLITE1",1)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("TEKLITE2",0)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("DOORBLU",0)==DOOM_EMISSIVE_LAMP);
    CHECK(doom_emissive_kind("EXITSIGN",0)==DOOM_EMISSIVE_PANEL);
    CHECK(doom_emissive_kind("FIREWALL",0)==DOOM_EMISSIVE_LAVA);
    CHECK(doom_emissive_kind("REDWALL",0)==DOOM_EMISSIVE_LAVA);
    CHECK(doom_emissive_kind("REDWALL1",0)==DOOM_EMISSIVE_LAVA);
    {const unsigned char bulb[]={120,0,0};
     CHECK(doom_emissive_pixel(doom_emissive_kind("DOORRED",0),bulb,255)>0);
     CHECK(doom_emissive_pixel(doom_emissive_kind("DOOR1",0),bulb,255)>0);}
    CHECK(doom_emissive_kind("BFALL2",0)==DOOM_EMISSIVE_LAVA);
    CHECK(doom_emissive_kind("BLAVAA1",1)==DOOM_EMISSIVE_LAVA);
    CHECK(doom_emissive_kind("SFALL1",0)==DOOM_EMISSIVE_SLIME);
    CHECK(doom_emissive_kind("COMP1OFF",0)==DOOM_EMISSIVE_NONE);
    CHECK(doom_emissive_kind("LITE4OFF",0)==DOOM_EMISSIVE_NONE);
    const byte rock[]={159,115,67}, crack[]={255,0,0}, yellowCrack[]={255,255,115};
    CHECK(doom_emissive_kind("RROCK05",1)==DOOM_EMISSIVE_MAGMA);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_MAGMA,rock,255)==0);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_MAGMA,crack,255)==255);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_MAGMA,yellowCrack,255)==255);
    const byte dimGreen[]={24,88,32}, brightGreen[]={24,100,32};
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_PANEL,dimGreen,255)>0);
    CHECK(doom_emissive_pixel(DOOM_EMISSIVE_PANEL,dimGreen,255)<
          doom_emissive_pixel(DOOM_EMISSIVE_PANEL,brightGreen,255));
    return 0;
}

static int flash_bounds(void)
{
    doom_flash_t light={0};light.position[3]=10;
    float low[3]={-100,-100,-100},high[3]={100,100,100};
    CHECK(doom_flash_reaches_bounds(&light,low,high)); /* Light inside a large triangle's bounds. */
    low[0]=10;high[0]=20;
    CHECK(doom_flash_reaches_bounds(&light,low,high)); /* Tangent retained. */
    low[0]=10.01f;
    CHECK(!doom_flash_reaches_bounds(&light,low,high));
    low[0]=8;low[1]=8;
    CHECK(!doom_flash_reaches_bounds(&light,low,high)); /* Corner outside sphere. */
    light.position[0]=12;light.position[1]=12;
    CHECK(doom_flash_reaches_bounds(&light,low,high)); /* Moving light enters bounds. */
    low[2]=11;
    CHECK(!doom_flash_reaches_bounds(&light,low,high)); /* Vertical separation. */
    return 0;
}

static int volumetric_fog(void)
{
    CHECK(doom_fog_transmittance(0,1)==1);
    CHECK(doom_fog_transmittance(-100,1)==1);
    CHECK(doom_fog_transmittance(1000,0)==1);
    CHECK(doom_fog_transmittance(100,1)>doom_fog_transmittance(1000,1));
    CHECK(doom_fog_transmittance(2000,1)>0.49f); /* Modest haze, even at maximum range. */
    CHECK(doom_fog_transmittance(2000,1)==doom_fog_transmittance(10000,1));
    float segment=doom_fog_transmittance(120,1);
    CHECK(fabsf(segment*segment-doom_fog_transmittance(240,1))<0.00001f);
    CHECK(doom_fog_beam_integral(0,100)==0);
    CHECK(doom_fog_beam_integral(-100,100)==0);
    CHECK(doom_fog_beam_integral(100,0)==0);
    CHECK(fabsf(doom_fog_beam_integral(100,100)-50)<0.00001f);
    CHECK(doom_fog_beam_integral(1000,100)==doom_fog_beam_integral(100,100));
    CHECK(doom_fog_beam_integral(25,100)<doom_fog_beam_integral(50,100));
    /* Compare the closed-form radial fade against a numerical reference. */
    float reference=0;
    for(int i=0;i<1000;++i) {
        float t=(i+0.5f)/1000,fade=1-t;
        reference+=fade*fade*(3-2*fade)*0.1f;
    }
    CHECK(fabsf(reference-doom_fog_beam_integral(100,100))<0.001f);
    return 0;
}

extern int maxammo[NUMAMMO], clipammo[NUMAMMO], pars[4][10], cpars[32];
extern cheatseq_t cheat_god, cheat_clev;
void A_Look();

static int dehacked_patch(void)
{
    /* Every vanilla block type plus the BEX sections, with CRLF endings. */
    FILE *file = fopen("portability.deh", "wb");
    CHECK(file);
    fputs("Patch File for DeHackEd v3.0\r\nDoom version = 21\r\n\r\n"
          "Thing 1 (Player)\r\nHit points = 150\r\nBits = SOLID+SHOOTABLE | DROPOFF\r\n\r\n"
          "Thing 12 (Imp)\r\nID # = 4242\r\nSpeed = 99\r\n\r\n"
          "Frame 100\r\nDuration = 77\r\nNext frame = 101\r\nSprite subnumber = 32773\r\n\r\n"
          "Pointer 7 (Frame 101)\r\nCodep Frame = 1\r\n\r\n"
          "Ammo 0 (Bullets)\r\nMax ammo = 400\r\nPer ammo = 20\r\n\r\n"
          "Weapon 1 (Pistol)\r\nAmmo type = 5\r\n\r\n"
          "Misc 0\r\nInitial Health = 150\r\nMonsters Infight = 202\r\n\r\n"
          "Cheat 0\r\nGod mode = iddqx\r\nLevel Warp = idwarp\r\n\r\n"
          "Text 4 4\r\nTROOZZZZ\r\nText 6 6\r\npistolpistl2\r\n"
          "Text 20 8\r\nPicked up the armor.New\r\nvest\r\n\r\n"
          "[STRINGS]\r\nGOTMEGA = Got the \\\r\n   mega!\\nNice.\r\nQUITMSG8 = stay\r\n"
          "OB_ZOMBIE = ignored\r\n\r\n"
          "[PARS]\r\npar 1 2 999 # comment\r\npar 15 77\r\n\r\n"
          "[CODEPTR]\r\nFRAME 102 = A_Look\r\nFRAME 103 = NULL\r\n", file);
    CHECK(fclose(file) == 0);
    char *args[] = {"portability", "-nodeh", "-deh", "portability.deh"};
    myargc = 4;
    myargv = args;
    actionf_v light0 = states[1].action.acv;
    DEH_Init();
    CHECK(mobjinfo[MT_PLAYER].spawnhealth == 150);
    CHECK(mobjinfo[MT_PLAYER].flags == (MF_SOLID | MF_SHOOTABLE | MF_DROPOFF));
    CHECK(mobjinfo[MT_TROOP].doomednum == 4242 && mobjinfo[MT_TROOP].speed == 99);
    CHECK(states[100].tics == 77 && states[100].nextstate == 101);
    CHECK(states[100].frame == 32773);
    CHECK(states[101].action.acv == light0);
    CHECK(states[102].action.acv == A_Look && states[103].action.acv == NULL);
    CHECK(maxammo[0] == 400 && clipammo[0] == 20);
    CHECK(weaponinfo[wp_pistol].ammo == am_noammo);
    CHECK(deh_initial_health == 150 && deh_species_infighting);
    CHECK(cheat_god.sequence[4] == SCRAMBLE('x') && cheat_god.sequence[5] == 0xff);
    CHECK(cheat_clev.sequence[6] == 1 && cheat_clev.sequence[9] == 0xff);
    CHECK(strcmp(sprnames[SPR_TROO], "ZZZZ") == 0);
    CHECK(strcmp(S_sfx[sfx_pistol].name, "pistl2") == 0);
    CHECK(strcmp(DEH_String(GOTARMOR), "New\nvest") == 0);
    CHECK(strcmp(DEH_String(GOTMEGA), "Got the mega!\nNice.") == 0);
    CHECK(strcmp(DEH_String(doom2_endmsg[1]), "stay") == 0);
    CHECK(strcmp(DEH_String(GOTHTHBONUS), GOTHTHBONUS) == 0);
    CHECK(pars[1][2] == 999 && cpars[14] == 77);
    return 0;
}

static int menu_background(void) {
    /* A wide rerelease page must repaint every pixel, centered, without
       writing beyond the software overlay. Also cover negative post clipping. */
    const int w=560, h=200, stride=205, header=8+560*4;
    byte *patch=calloc(1,header+w*stride), *buffer=malloc(SCREENWIDTH*SCREENHEIGHT+2);
    CHECK(patch && buffer);
    put16(patch,w);put16(patch+2,h);
    for(int x=0;x<w;++x){byte *post=patch+header+x*stride;
        put32(patch+8+x*4,header+x*stride);post[1]=h;
        memset(post+3,x%251,h);post[204]=255;}
    memset(buffer,253,SCREENWIDTH*SCREENHEIGHT+2);screens[0]=buffer+1;
    V_DrawPatch(0,0,0,(patch_t*)patch);
    CHECK(buffer[0]==253 && buffer[SCREENWIDTH*SCREENHEIGHT+1]==253);
    for(int y=0;y<SCREENHEIGHT;++y)for(int x=0;x<SCREENWIDTH;++x)
        CHECK(screens[0][y*SCREENWIDTH+x]==(x+120)%251);
    V_DrawPatch(-4,-3,0,(patch_t*)patch);
    CHECK(screens[0][0]==4 && buffer[0]==253 && buffer[SCREENWIDTH*SCREENHEIGHT+1]==253);
    screens[0]=NULL;free(buffer);free(patch);return 0;
}

int main(void)
{
    CHECK(SwapSHORT(UINT16_C(0x1234)) == UINT16_C(0x3412));
    CHECK(SwapLONG(UINT32_C(0x12345678)) == UINT32_C(0x78563412));
    CHECK(SwapLONG(SwapLONG(UINT32_C(0xdeadbeef))) == UINT32_C(0xdeadbeef));
    CHECK(I_GetTime() == 0);
    I_WaitVBL(3);
    CHECK(I_GetTime() >= 1);
    Z_Init();
    CHECK(menu_background() == 0);
    CHECK(movement() == 0);
    CHECK(sprite_glow() == 0);
    CHECK(emissive_masks() == 0);
    CHECK(flash_lighting() == 0);
    CHECK(flash_culling() == 0);
    CHECK(flash_bounds() == 0);
    CHECK(volumetric_fog() == 0);
    CHECK(allocator() == 0);
    CHECK(configuration() == 0);
    CHECK(saved_player() == 0);
    CHECK(texture_fixture() == 0);
    CHECK(dehacked_patch() == 0); /* Last: it patches the shared tables. */
    puts("Portability checks passed.");
    return 0;
}
