/* The scene's public state, its lifecycle and the per-frame entry points
   (prepareFrame), plus the backend-independent I_Render3D calls. */
#include "internal.h"
#include "../shaders/feature_flags.h"
#include "modern_hud.h"
#include "weapon_wheel.h"
#include "key_notice.h"
#include "toasty.h"
#include "../mod_soundbank.h"
extern "C" int p_fluidfastpistol;
extern "C" int p_player_surface_damage,p_monster_surface_damage,p_breakablewalls,p_throughwalls,p_breachdoors,p_wall_damage,p_deeperdig;
extern "C" {
#include "p_fluid.h"
void I_SetFluidSoundAddon(int enabled);
void I_SetNearMissSounds(int enabled);
}
extern "C" {
extern int r_modernhud;
#include "../audio/sound_options.h"
#include "../../linuxdoom-1.10/p_local.h"
}

namespace doom3d {
Profile profile;
Settings settings;
SDL_Window *gameWindow;
FlashSet flashes;
FogLights fogLights;
std::vector<LightBlocker> lightBlockers;
std::unordered_map<int,Image> images;
std::map<const std::vector<Vertex>*,std::vector<VertexSection>> vertexSections;
std::map<int,std::vector<Vertex>> batches,shadowBatches,decalBatches,effectBatches,effectAddBatches,mirrorBatches;
std::vector<Vertex> mistVertices,particleVertices,heatVertices,skyVertices,shaftVertices;
GpuTextureRef seamTexture,contactTexture,cloudTexture,wallBakeTexture,flatLightTexture,causticTexture,causticPattern,shoreTexture;
GpuTextureRef wallDirectionTexture,flatDirectionTexture;
float reflectionPlane=0;
bool reflectionActive=false;
bool worldPending,screenshotPending,flashlightOn=false;
float pitch;
unsigned levelSerial;
static float oldEye[3], oldYaw;

std::vector<std::array<float,2>> oldHeights;
float renderFraction=1;
uint64_t tickTime;
static uint64_t titleTime;
static int frameCount;
static float fps,frameMs,peakFrameMs;
static uint64_t previousFrame;
static float framePeak;
static std::unordered_map<const mobj_t*, std::array<float,3>> oldThings;
void interpolatedPosition(const mobj_t &thing,float fraction,float &x,float &y,float &z) {
    x=units(thing.x);y=units(thing.y);z=units(thing.z);
    auto old=oldThings.find(&thing);
    if(old!=oldThings.end()&&std::hypot(x-old->second[0],y-old->second[1])<128) {
        x=old->second[0]+(x-old->second[0])*fraction;
        y=old->second[1]+(y-old->second[1])*fraction;
        z=old->second[2]+(z-old->second[2])*fraction;
    }
}
static void beforeTic() {
    if(gamestate!=GS_LEVEL||!players[displayplayer].mo||paused) {tickTime=0;return;}
    mobj_t *mo=players[displayplayer].mo;
    snapshotWeaponPositions();
    oldEye[0]=units(mo->x);oldEye[1]=units(mo->y);oldEye[2]=units(players[displayplayer].viewz);
    oldYaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));tickTime=SDL_GetTicksNS();
    oldThings.clear();
    oldHeights.resize(numsectors);
    for(int i=0;i<numsectors;++i) oldHeights[i]={units(sectors[i].floorheight),units(sectors[i].ceilingheight)};
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        {oldThings.emplace(thing,std::array<float,3>{units(thing->x),units(thing->y),units(thing->z)});fluidProjectileFlight(*thing);}
    tickImpactParticles();
    tickFluidEffects();
    watchBodies();
    watchSplashes();
    watchGlass();
    watchShotLights();
}
static void acceleratedView(player_t *player) {
    (void)player; worldPending=true; v_overlayactive=true;
    memset(v_overlaymask,255,sizeof(v_overlaymask));
    for(int y=viewwindowy;y<viewwindowy+viewheight;++y) {
        memset(screens[0]+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
        memset(v_overlaymask+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
    }
}
void applySettings() {
    p_breakablewalls=settings.breakableWalls;
    p_throughwalls=settings.throughWalls;
    p_breachdoors=settings.breachDoors;
    p_deeperdig=settings.deeperDig;
    p_monster_surface_damage=settings.monsterSurfaceDamage;
    p_player_surface_damage=settings.playerSurfaceDamage;
    p_wall_damage=std::clamp(settings.wallDamage,100,500);
    if(p_fluidweapons!=settings.fluidWeapons||p_fluidmonsters!=settings.fluidMonsters) {
        P_FluidReset();resetWeaponPositions();
    }
    p_fluidweapons=settings.fluidWeapons;p_fluidmonsters=settings.fluidMonsters;
    p_persistentpursuit=settings.persistentPursuit;
    P_SetAggressiveAI(settings.aggressiveAI);
    P_SetAdvancedTactics(settings.advancedTactics);
    I_SetFluidSoundAddon(settings.fluidSounds);
    I_SetNearMissSounds(settings.nearMissSounds);
    p_fluidfastpistol=settings.fluidFastPistol;
    p_noticecombat=settings.combatNotices;
    if(gameWindow&&bool(SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_FULLSCREEN)!=bool(settings.fullscreen))
        SDL_SetWindowFullscreen(gameWindow,settings.fullscreen!=0);
    R_AcceleratedView=settings.accelerated?acceleratedView:nullptr;
    r_modernhud=settings.accelerated&&settings.hudStyle!=0;
    R_BeforeTic=settings.accelerated?beforeTic:nullptr;
    R_MuzzleFlash=settings.accelerated?fluidWeaponShot:nullptr;
    R_WeaponAttack=settings.accelerated?fluidWeaponAttack:nullptr;
    R_WeaponAux=settings.accelerated&&P_FluidWeaponsEnabled()?fluidWeaponAux:nullptr;
    p_destructibledoors=0; // Breaching prototype withdrawn; ignore old config values.
    R_RocketImpact=settings.accelerated?fluidProjectileImpact:nullptr;
    R_BFGHit=settings.accelerated?fluidBFGHit:nullptr;
    R_DoorMotion=settings.accelerated?doorMotion:nullptr;
    R_CraterCrumble=settings.accelerated?craterCrumble:nullptr;
    R_SurfaceChip=settings.accelerated?surfaceChip:nullptr;
    R_SolidBulletImpact=settings.accelerated?solidBulletImpact:nullptr;
    R_DoorDamage=nullptr;
    R_WallImpact=settings.accelerated?wallImpact:nullptr;
    R_DamageHit=settings.accelerated?bodyDamaged:nullptr;
    R_BloodHit=settings.accelerated?bloodHit:nullptr;
    R_PlaneImpact=settings.accelerated?planeImpact:nullptr;
    R_ShotPassed=settings.accelerated?glassShot:nullptr;
    freeaim=settings.accelerated&&settings.look;
    if(!settings.accelerated) clearFlashPulses();
    r_uncapped=settings.accelerated;
    if(!settings.look) pitch=0;
    R_SetViewSize(10,0);
    I_SetSoundOptions(settings.roomAcoustics,settings.reverbAmount,settings.muffleWalls,settings.muffleDistance);
    I_SetSfxPlayback(settings.sfxResampling,settings.sfxTreble);
    I_SetMadeSounds(settings.madeImpacts,settings.glassSound,settings.ambience,settings.ambienceAmount,settings.footsteps);
    I_AmbienceShowGlass(settings.accelerated&&settings.windowGlass);
    I_SetMusicDevice(settings.musicDevice);
}
void sceneInit(SDL_Window *window) {
    gameWindow=window;
    int config=M_CheckParm((char*)"-graphics");
    if(config&&config+1<myargc)graphicsConfig=myargv[config+1];
    loadSettings();if(M_CheckParm((char*)"-classic"))settings.accelerated=0;
    if(M_CheckParm((char*)"-rendercheck"))settings.accelerated=1;
    int fov=M_CheckParm((char*)"-fov");if(fov&&fov+1<myargc)settings.fov=std::clamp((float)atof(myargv[fov+1]),60.0f,120.0f);
    flashlightOn=M_CheckParm((char*)"-flashlight")!=0;
    int tintOption=M_CheckParm((char*)"-flashlighttint");
    if(tintOption&&tintOption+1<myargc)
        settings.flashlightTintGain=std::clamp((float)atof(myargv[tintOption+1]),0.0f,2.0f);
    int fogOption=M_CheckParm((char*)"-fog");
    if(fogOption&&fogOption+1<myargc)settings.fog=atoi(myargv[fogOption+1])!=0;
    int paletteOption=M_CheckParm((char*)"-palette");
    if(paletteOption&&paletteOption+1<myargc)settings.palette=atoi(myargv[paletteOption+1])!=0;
}
void sceneShutdown() {
    toastyTexture.reset();toastyVisible=0;
    stopRelight();
    R_AcceleratedView=nullptr;R_BeforeTic=nullptr;R_MuzzleFlash=nullptr;R_WallImpact=nullptr;R_RocketImpact=nullptr;R_BFGHit=nullptr;R_DoorMotion=nullptr;R_DoorDamage=nullptr;R_CraterCrumble=nullptr;R_SurfaceChip=nullptr;R_SolidBulletImpact=nullptr;R_DamageHit=nullptr;R_BloodHit=nullptr;R_PlaneImpact=nullptr;R_ShotPassed=nullptr;r_uncapped=0;v_overlayactive=0;
    lightBlockers.clear();flashes={};oldThings.clear();oldHeights.clear();
    vertexSections.clear();batches.clear();shadowBatches.clear();decalBatches.clear();effectBatches.clear();effectAddBatches.clear();mirrorBatches.clear();particleVertices.clear();
    mistVertices.clear();skyVertices.clear();shaftVertices.clear();heatVertices.clear();
    imagesShutdown();mapShutdown();causticsShutdown();clearSurfaceLights();lightsShutdown();exposureShutdown();
    bakeAtlasShutdown();sunShutdown();lightBakeShutdown();clearStaticGrid();indirectShutdown();
    bodyMotionShutdown();impactsShutdown();bloodShutdown();splashesShutdown();glassShutdown();shotLightsShutdown();
}
// A new level, or the first after shutdown: each part forgets the last
// level, then the map and what is built from it at level load.
static void startLevel() {
    loadingStage(LoadStage::Map);
    stopRelight();
    vertexSections.clear();batches.clear();shadowBatches.clear();mirrorBatches.clear();clearSurfaceLights();shotLightsShutdown();
    bakeAtlasNewLevel();sunNewLevel();lightBakeNewLevel();clearStaticGrid();indirectNewLevel();
    oldThings.clear();oldHeights.clear();tickTime=0;
    resetWeaponPositions();
    bodyMotionShutdown();
    buildMap();
    prepareFalls();
    buildCaustics();
    buildShore();
    layoutBake();
    measureEmissionDensity();
    findDoors();
    levelSerial=r_levelserial;
}
static bool levelPending() {return levelSerial!=r_levelserial||floors.empty();}
bool bakesPending() {
    return (settings.bakedLights&&lightBakeKey!=lightBakeInputs())||(settings.bounce&&bounceKey!=bounceInputs())||
        ((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)||(settings.lightFlow&&flowKey!=flowInputs())||(settings.sun&&!sunBaked);
}
bool loadPending() {return worldPending&&(levelPending()||settings.thingShadows!=bakeThingsKey||bakesPending());}
// What this frame builds and bakes, for the loading bar. A new level resets
// every bake, so each one switched on runs.
static void planStages(bool level) {
    bool lights=settings.bakedLights&&(level||lightBakeKey!=lightBakeInputs());
    planLoading(LoadStage::Map,level);planLoading(LoadStage::Textures,level);
    planLoading(LoadStage::Sun,settings.sun&&(level||!sunBaked));
    planLoading(LoadStage::Lights,lights);
    planLoading(LoadStage::Grid,settings.bakedLights&&settings.gridSpriteLight&&(lights||staticSerial!=lightBakeSerial));
    planLoading(LoadStage::Bounce,settings.bounce&&(level||lights||bounceKey!=bounceInputs()));
    planLoading(LoadStage::Ambient,(settings.skyLight||settings.bakedAO)&&(level||ambientKey!=bakeThingsKey));
    planLoading(LoadStage::Flow,settings.lightFlow&&(level||flowKey!=flowInputs()));
}
// Binds a batch texture with its next animation frame and crossfade amount.
// The engine switches frames before leveltime++, so the phase uses leveltime-1.
SurfaceBinding surfaceBinding(int key) {
    Image &image=images.at(key);
    const Image *next=&image;float blend=0;
    int pic=key>=0?key:-1-key-firstflat,nextPic=0;
    bool flat=key<0&&pic>=0&&pic<numflats;
    int speed=key>=0||flat?P_PicAnimationNext(key>=0,pic,&nextPic):0;
    if((key>=0||flat)&&leveltime>0) {
        if(speed>0) {
            const Image &candidate=key>=0?wallImage(nextPic):lumpImage(firstflat+nextPic,true);
            if(candidate.width==image.width&&candidate.height==image.height) {
                next=&candidate;blend=((leveltime-1)%speed+renderFraction)/speed;
            }
        }
    }
    SurfaceBinding binding={&image,next,blend};
    // Cache cycle means once per level, not per draw or animation frame.
    static unsigned meanLevel=~0u;
    static std::unordered_map<int,std::array<float,4>> means;
    if(meanLevel!=r_levelserial){means.clear();meanLevel=r_levelserial;}
    if(flat&&speed>0) {
        const char *name=lumpinfo[firstflat+pic].name;
        bool liquid=!strncmp(name,"FWATER",6)||!strncmp(name,"NUKAGE",6)||!strncmp(name,"SLIME",5)||!strncmp(name,"LAVA",4)||!strncmp(name,"BLOOD",5);
        if(liquid) {
            auto found=means.find(pic);
            if(found==means.end()) {
                std::vector<int> frames;std::array<float,4> mean={};int at=pic;
                for(int n=0;n<64;++n) {
                    if(std::find(frames.begin(),frames.end(),at)!=frames.end())break;
                    frames.push_back(at);const auto &rgb=averageColor(lumpImage(firstflat+at,true));
                    for(int c=0;c<3;++c)mean[c]+=rgb[c];
                    int following;if(P_PicAnimationNext(false,at,&following)<=0)break;at=following;
                }
                for(int c=0;c<3;++c)mean[c]/=frames.size();
                mean[3]=.35f;
                for(int frame:frames)means[frame]=mean;
                found=means.find(pic);
            }
            binding.liquidCycle=found->second;
            const auto &a=averageColor(image),&b=averageColor(const_cast<Image&>(*next));
            for(int c=0;c<3;++c)binding.liquidFrame[c]=a[c]+(b[c]-a[c])*blend;
        }
    }
    return binding;
}
void screenQuad(std::vector<Vertex> &out,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float light,unsigned mode) {
    quad(out,{x,y,0,u0,v0,light,mode},{x+w,y,0,u1,v0,light,mode},
        {x+w,y-h,0,u1,v1,light,mode},{x,y-h,0,u0,v1,light,mode});
}
// Profiling: this frame's lights, blockers and how many triangles they reach.
static void recordProfile() {
    I_Render3DFrameProfile &frame=profile.frame;
    frame.world=worldPending;frame.reflection=reflectionActive;frame.lights=(int)flashes.count;
    for(unsigned i=0;i<flashes.count;++i) {
        const Flash &f=flashes.lights[i];
        const mobj_t *source=lightSources[i];
        frame.light[i]={f.position[0],f.position[1],f.position[2],f.position[3],lightKinds[i],source?(int)source->type:-1,(int)f.count,0,lightSliceCost[i]};
        frame.blockers+=(int)f.count;
    }
    for(const auto &batch:batches) for(size_t n=0;n+2<batch.second.size();n+=3) {
        const unsigned *mask=batch.second[n].lightMask;
        ++frame.triangles;
        if(mask[0]|mask[1])++frame.litTriangles;
        for(unsigned i=0;i<flashes.count;++i) if(mask[i/32]&(1u<<(i%32))) {++frame.light[i].triangles;++frame.lightTriangles;}
    }
}
FrameView prepareFrame(int w,int h) {
    const bool spikes=M_CheckParm((char*)"-profilespikes")!=0;
    uint64_t diag=spikes?SDL_GetTicksNS():0;double stages[6]={};
    auto stamp=[&](int n){if(spikes){auto now=SDL_GetTicksNS();stages[n]=(now-diag)/1e6;diag=now;}};
    FrameView view={};Uniforms &camera=view.camera;
    uint64_t prepareStart=profileStart();
    static unsigned profileSerial;
    if(profile.enabled) {profile.frame=I_Render3DFrameProfile{};profile.frame.serial=++profileSerial;pitch=0;}
    camera.projection[2]=1;camera.projection[3]=32768;
    camera.effects[0]=settings.filter;camera.effects[1]=settings.retro;
    camera.texFilter[0]=settings.sharpSoftness;camera.texFilter[1]=settings.paletteMips&&settings.filter;
    camera.effects[2]=players[displayplayer].fixedcolormap;
    camera.effects[3]=settings.spriteFilter;camera.materials[0]=settings.emissive;
    unsigned features=(settings.detail?FEATURE_SURFACE_DETAIL:0)|(settings.retroReflections?FEATURE_RETRO_REFLECTIONS:0)|
        (settings.caustics&&causticTexture?FEATURE_CAUSTICS:0)|(settings.texelLight?FEATURE_TEXEL_BAKE:0)|
        (settings.caustics&&settings.ceilingCaustics&&causticTexture?FEATURE_CEILING_CAUSTICS:0)|
        (settings.lightFlow&&flowKey>=0&&flowKey==flowInputs()?FEATURE_LIGHT_FLOW:0)|
        (settings.bakedAO&&ambientKey>=0&&ambientKey==bakeThingsKey?FEATURE_BAKED_OCCLUSION:0)|
        (settings.glossyScreens?FEATURE_GLOSSY_SCREENS:0)|(settings.causticsComputed?FEATURE_CAUSTICS_COMPUTED:0)|
        (settings.causticsGrow?FEATURE_CAUSTICS_GROW:0)|(settings.causticsAngle?FEATURE_CAUSTICS_ANGLE:0)|
        (settings.causticsSway?FEATURE_CAUSTICS_SWAY:0)|(settings.causticsSprites?FEATURE_CAUSTICS_SPRITES:0)|
        (settings.causticsShots?FEATURE_CAUSTICS_SHOTS:0)|(settings.dampShores&&shoreTexture?FEATURE_DAMP_SHORES:0)|
        (settings.detail&&settings.variedHighlights?FEATURE_VARIED_HIGHLIGHTS:0)|
        (settings.windowLight&&bakedLightsActive?FEATURE_WINDOW_LIGHT:0)|
        (settings.detail&&settings.unevenTiles?FEATURE_UNEVEN_TILES:0)|(settings.detail&&settings.unevenWalls?FEATURE_UNEVEN_WALLS:0)|
        (settings.classicFlashlight?FEATURE_CLASSIC_FLASHLIGHT:0)|(settings.stippledShadows?FEATURE_STIPPLED_SHADOWS:0)|(settings.glowEdges&&shoreTexture&&causticPattern?FEATURE_GLOW_EDGES:0);
    camera.flashlightTint[3]=(float)features; // see shaders/feature_flags.h
    camera.map[0]=mapOrigin[0];camera.map[1]=mapOrigin[1];camera.map[2]=1/mapCell;
    camera.map[3]=settings.softLight&&seamTexture&&worldPending;
    camera.materials[2]=settings.fog&&worldPending;
    camera.materials[3]=settings.palette&&worldPending;
    // No bloom while the in-game settings panel covers the view: it would glow
    // through the dimmed backdrop and soften the panel's text.
    camera.materials[1]=settings.emissive&&worldPending&&camera.effects[2]==0&&!I_Render3DSettingsOpen();
    camera.fx[0]=1;
    float uiWidth=std::min((float)w,h*4.0f/3),uiHeight=uiWidth*0.75f,uiX=(w-uiWidth)/2;
    float uiY=worldPending&&settings.widescreen?h-uiHeight:(h-uiHeight)/2;
    float worldX=uiX+uiWidth*viewwindowx/320,worldY=uiY+uiHeight*viewwindowy/200.0f;
    float worldW=uiWidth*scaledviewwidth/320,worldH=uiHeight*viewheight/200.0f;
    if(settings.widescreen&&scaledviewwidth==320)
    { worldX=0;worldY=0;worldW=w;worldH=h-uiHeight*(1-viewheight/200.0f); }
    float referenceAspect=(4.0f/3)*(scaledviewwidth/320.0f)/(viewheight/200.0f);
    camera.projection[1]=referenceAspect/tan(settings.fov*doomPi/360);
    camera.projection[0]=camera.projection[1]/(worldW/std::max(1.0f,worldH));
    camera.fx2[0]=worldH/200;
    float fraction=1;
    if(worldPending) {
        bool level=levelPending();
        if(level) {planStages(true);startLevel();}
        syncBakeThings();
        if(!level)planStages(false);
        relightMovingSectors();
        stamp(0);
        if(settings.sun&&!sunBaked)bakeSun();
        bool sun=settings.sun&&sunLevel>0;
        for(int c=0;c<3;++c)camera.sun[c]=sun?sunTint[c]:0;
        camera.sun[3]=sun?sunLevel:0;
        camera.fx2[2]=sunYaw;camera.fx2[3]=sun&&settings.sunDisc?sunGlow:0;
        bakedLightsActive=settings.bakedLights&&lightBakeKey==lightBakeInputs();
        bakeOnlyActive=bakedLightsActive&&(lightBakeKey&2);
        // bake.z: 1 bake-only lights (directions and flicker groups), -1
        // flicker groups only, for lamps that switch with their sector.
        bool directions=wallDirectionTexture&&flatDirectionTexture;
        camera.bake[0]=bakedLightsActive;camera.bake[2]=directions?(bakeOnlyActive?1.0f:lampGroupCount>0?-1.0f:0.0f):0.0f;
        if(const mobj_t *viewer=players[displayplayer].mo) {
            float at[3]={units(viewer->x),units(viewer->y),0};
            updateLampGroups(at);
        }
        for(int g=0;g<flickerGroups;++g)camera.flicker[g/4][g%4]=groupFlicker(g);
        player_t *player=&players[displayplayer];mobj_t *mo=player->mo;
        // Generated material galleries identify each room without replacing any art.
        if(int lump=W_CheckNumForName((char*)"DTDGALRY");lump>=0){
            static int lastRoom=-1;static unsigned lastLevel=~0u;static char caption[80];
            if(lastLevel!=r_levelserial){lastRoom=-1;lastLevel=r_levelserial;}
            int sector=int(mo->subsector->sector-sectors),room=0;
            const byte* data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);int length=W_LumpLength(lump);
            for(int at=0;at+36<=length;at+=36)if((data[at]|data[at+1]<<8)==sector){
                room=data[at+2]|data[at+3]<<8;
                if(room!=lastRoom){snprintf(caption,sizeof(caption),"%03d %.8s / %.8s / %.8s",room,data+at+4,data+at+12,data+at+20);player->message=caption;}
                break;
            }
            lastRoom=room;
        }
        camera.eye[0]=units(mo->x);camera.eye[1]=units(mo->y);camera.eye[2]=units(player->viewz);
        float yaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));
        if(tickTime&&!paused&&!menuactive&&!demoplayback) {
            fraction=std::clamp((SDL_GetTicksNS()-tickTime)/(1e9/TICRATE),0.0,1.0);
            if(std::hypot(camera.eye[0]-oldEye[0],camera.eye[1]-oldEye[1])<128) {
                for(int i=0;i<3;++i)camera.eye[i]=oldEye[i]+(camera.eye[i]-oldEye[i])*fraction;
                yaw=oldYaw+std::remainder(yaw-oldYaw,2*doomPi)*fraction;
            }
        }
        float look=demoplayback?0:pitch;
        tourpitch=look;freeaim=settings.look&&!demoplayback;
        camera.right[0]=sin(yaw);camera.right[1]=-cos(yaw);
        camera.forward[0]=cos(yaw)*cos(look);camera.forward[1]=sin(yaw)*cos(look);camera.forward[2]=sin(look);
        camera.up[0]=-cos(yaw)*sin(look);camera.up[1]=-sin(yaw)*sin(look);camera.up[2]=cos(look);
        camera.fog[0]=0.00045f*(player->mo->subsector->sector->ceilingpic==skyflatnum?0.35f:1.0f);
        camera.fog[1]=0.012f;camera.fog[2]=fogReferenceHeight;
        camera.fog[3]=(leveltime+fraction)/TICRATE;
        renderFraction=fraction;
        // Caustics follow the liquid's frame and crossfade like its floor (surfaceBinding).
        if(!causticFrames.empty()&&causticSpeed>0&&leveltime>0) {
            auto at=std::find(causticFrames.begin(),causticFrames.end(),flattranslation[causticFrames[0]]);
            camera.texFilter[2]=at!=causticFrames.end()?float(at-causticFrames.begin()):0;
            camera.texFilter[3]=((leveltime-1)%causticSpeed+fraction)/causticSpeed;
        }
        uint64_t geometryStart=profileStart();
        stamp(1);
        if(level)loadingStage(LoadStage::Textures);
        geometry(camera,fraction);
        stamp(2);
        profileAdd(profile.frame.geometryMs,geometryStart);
        if(profile.enabled)skipProfiledLights();
        // Surface lights come from this frame's geometry; the bake applies from the next frame.
        if(settings.bakedLights&&lightBakeKey!=lightBakeInputs())bakeLights();
        if(settings.bakedLights&&settings.gridSpriteLight&&lightBakeKey==lightBakeInputs()&&staticSerial!=lightBakeSerial)bakeStaticGrid();
        if(settings.bounce&&bounceKey!=bounceInputs())bakeBounce();
        if((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)bakeAmbient();
        if(settings.lightFlow&&flowKey!=flowInputs())bakeFlow();
        camera.bake[1]=settings.bounce&&bounceKey>=0?bounceStrength:0;
        camera.bake[3]=camera.bake[1]>0?settings.bounceTint:0;
        bool ambient=ambientKey==bakeThingsKey;
        camera.fx2[1]=settings.skyLight&&ambient?skyLevel:0;
        // The fog haze takes the sky's color too, so it follows the sky without the sky-light bake.
        static int tintedSky=-1;
        if(skytexture!=tintedSky){skyTint=skyColor();tintedSky=skytexture;}
        for(int c=0;c<3;++c)camera.fx[1+c]=skyTint[c];
        buildMist(camera);
        buildShafts(camera);
        buildRings(camera);
        updateExposure(camera);
        updateFlashlightTint(camera);
        selectFogLights(camera);
        if(profile.skip&I_RENDER3D_SKIP_MIST) {mistVertices.clear();shaftVertices.clear();}
        if(profile.skip&I_RENDER3D_SKIP_FOG) {fogLights={};camera.materials[2]=0;}
        if(profile.skip&I_RENDER3D_SKIP_BLOOM) camera.materials[1]=0;
    }
    if(!worldPending) {mistVertices.clear();shaftVertices.clear();heatVertices.clear();skyVertices.clear();}
    uint64_t masksStart=profileStart();
    stamp(3);
    assignLightMasks();
    stamp(4);
    profileAdd(profile.frame.masksMs,masksStart);
    uint64_t wordsStart=profileStart();
    buildLightWords();
    stamp(5);
    if(spikes) {
        double sum=0;for(double t:stages)sum+=t;
        if(sum>14)fprintf(stderr,"Prepare spike tic %d: relight %.3f camera %.3f geometry %.3f effects %.3f masks %.3f packing %.3f ms\n",gametic,stages[0],stages[1],stages[2],stages[3],stages[4],stages[5]);
    }
    profileAdd(profile.frame.lightsMs,wordsStart);
    if(profile.enabled) {recordProfile();profileAdd(profile.frame.prepareMs,prepareStart);}
    view.uiX=uiX;view.uiY=uiY;view.uiWidth=uiWidth;view.uiHeight=uiHeight;
    view.worldX=worldX;view.worldY=worldY;view.worldW=worldW;view.worldH=worldH;
    return view;
}
std::vector<unsigned> hudPixels(const unsigned *pixels) {
    std::vector<unsigned> overlay(pixels,pixels+SCREENWIDTH*SCREENHEIGHT);
    if(worldPending) for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;++i)overlay[i]=(overlay[i]&0xffffff)|(unsigned)v_overlaymask[i]<<24;
    std::vector<unsigned> modern(SCREENWIDTH*SCREENHEIGHT,0);
    modernHud(modern);
    keyNotice(overlay);
    toastyNotice();
    wheelDraw(overlay);
    if(settings.fps&&worldPending&&!menuactive&&fps>0) {
        // Readable in fullscreen, independent of HUD layout and scale.
        char lines[2][48];snprintf(lines[0],48,"%.0f FPS",fps);
        snprintf(lines[1],48,"%.1f MS / MAX %.1f",frameMs,peakFrameMs);
        auto glyph=[](char c)->const Image* {
            if(c==' ')return nullptr;
            char name[9];snprintf(name,sizeof(name),"STCFN%03d",int((unsigned char)c));
            int lump=W_CheckNumForName(name);return lump<0?nullptr:&lumpImage(lump,false);
        };
        const byte *pal=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        for(int row=0;row<2;++row) {
            int width=0;for(char c:std::string(lines[row])){auto im=glyph(c);width+=im?im->width:4;}
            int x=(SCREENWIDTH-width)/2,y=3+row*12;
            for(int yy=y-1;yy<std::min(SCREENHEIGHT,y+10);++yy)
                for(int xx=std::max(0,x-3);xx<std::min(SCREENWIDTH,x+width+3);++xx)overlay[yy*SCREENWIDTH+xx]=0xd010151a;
            for(char c:std::string(lines[row])) {
                auto im=glyph(c);if(!im){x+=4;continue;}
                for(int yy=0;yy<im->height;++yy)for(int xx=0;xx<im->width;++xx) {
                    size_t at=(yy*im->width+xx)*2;
                    if(!im->pixels[at+1]||x+xx<0||x+xx>=SCREENWIDTH||y+yy>=SCREENHEIGHT)continue;
                    auto rgb=pal+im->pixels[at]*3;unsigned shade=std::max({rgb[0],rgb[1],rgb[2]});
                    overlay[(y+yy)*SCREENWIDTH+x+xx]=0xff000000|(shade<<16)|(shade<<8)|shade;
                }
                x+=im->width;
            }
        }
    }
    overlay.insert(overlay.end(),modern.begin(),modern.end());
    return overlay;
}
// Current sector light and heights for seam blending and contact shading;
// w is 0 indoors, and under a sky packOutdoor.
std::vector<std::array<float,4>> sectorInfo() {
    std::vector<std::array<float,4>> info(std::max(1,numsectors));
    if(worldPending)for(int i=0;i<numsectors;++i)
        info[i]={lighting(&sectors[i]),floorZ(&sectors[i]),ceilZ(&sectors[i]),
            sectors[i].ceilingpic==skyflatnum?packOutdoor(&sectors[i]):0.0f};
    return info;
}
bool renderCheckDue() {return M_CheckParm((char*)"-rendercheck")&&worldPending&&gametic>10;}
void saveScreenshot(const void *bgra,int width,int height,int stride) {
    SDL_CreateDirectory("Screenshots");
    char path[256];snprintf(path,sizeof(path),"Screenshots/DOOM-%llu.png",(unsigned long long)SDL_GetTicksNS());
    int test=M_CheckParm((char*)"-rendercheck");
    const char *output=test&&test+1<myargc?myargv[test+1]:profile.screenshotPath?profile.screenshotPath:path;
    profile.screenshotPath=nullptr;
    SDL_Surface *surface=bgra?SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_BGRA32,(void*)bgra,stride):nullptr;
    bool saved=surface&&SDL_SavePNG(surface,output);SDL_DestroySurface(surface);
    if(!test) players[consoleplayer].message=(char*)(saved?"SCREENSHOT SAVED":"SCREENSHOT FAILED");
    fprintf(stderr,"Screenshot %s: %s (%dx%d).\n",saved?"saved":"failed",output,width,height);
    if(test) {if(!saved)I_Error((char*)"Render check could not save image");I_Quit();}
}
void updateTitle(int width,int height,const char *renderer) {
    ++frameCount;
    uint64_t now=SDL_GetTicksNS();
    if(previousFrame)framePeak=std::max(framePeak,float((now-previousFrame)/1e6));
    previousFrame=now;
    if(!titleTime){titleTime=now;frameCount=0;}
    if(now-titleTime>1000000000) {
        fps=frameCount*1e9/(now-titleTime);frameMs=1000.f/std::max(fps,1.f);peakFrameMs=framePeak;framePeak=0;
        frameCount=0;titleTime=now;
        char rate[32]="";if(settings.fps)snprintf(rate,sizeof(rate)," · %.0f fps",fps);
        char title[256];snprintf(title,sizeof(title),"DOOM · %s · %dx%d%s · F: flashlight · F4: graphics · F7: screenshot · Esc: menu",
            settings.accelerated?renderer:"Classic",width,height,rate);
        SDL_SetWindowTitle(gameWindow,title);
    }
}
} // namespace doom3d


// Backend-independent parts of the renderer interface.
using namespace doom3d;
void I_Render3DStartFrame(void) {worldPending=false;v_overlayactive=false;}
#ifdef DOOM_TEST_DRIVER
extern "C" void I_TestBundledModSettings() {
    int weapons=settings.fluidWeapons,sounds=settings.fluidSounds,monsters=settings.fluidMonsters;
    int digSetting=settings.breakableWalls;
    for(int value=0;value<2;++value){settings.breakableWalls=value;applySettings();if(p_breakablewalls!=value)I_Error((char*)"Breakable walls live toggle failed");}
    settings.breakableWalls=digSetting;applySettings();
    int throughSetting=settings.throughWalls;
    for(int value=0;value<2;++value){settings.throughWalls=value;applySettings();if(p_throughwalls!=value)I_Error((char*)"Through-wall breach live toggle failed");}
    settings.throughWalls=throughSetting;applySettings();
    int doorsSetting=settings.breachDoors;
    for(int value=0;value<2;++value){settings.breachDoors=value;applySettings();if(p_breachdoors!=value)I_Error((char*)"Door breach live toggle failed");}
    settings.breachDoors=doorsSetting;applySettings();
    int damageSetting=settings.wallDamage;
    for(int value:{100,250,500}){settings.wallDamage=value;applySettings();if(p_wall_damage!=value)I_Error((char*)"Wall damage live setting failed");}
    settings.wallDamage=damageSetting;applySettings();
    int deeperSetting=settings.deeperDig;
    for(int value=0;value<2;++value){settings.deeperDig=value;applySettings();if(p_deeperdig!=value)I_Error((char*)"Deeper digging live toggle failed");}
    settings.deeperDig=deeperSetting;applySettings();
    int aggressiveSetting=settings.aggressiveAI;
    for(int value=0;value<2;++value){settings.aggressiveAI=value;applySettings();if(p_aggressiveai!=value)I_Error((char*)"Aggressive AI live toggle failed");}
    settings.aggressiveAI=aggressiveSetting;applySettings();
    int pursuitSetting=settings.persistentPursuit;
    for(int value=0;value<2;++value){settings.persistentPursuit=value;applySettings();if(p_persistentpursuit!=value)I_Error((char*)"Pursuit live toggle failed");}
    settings.persistentPursuit=pursuitSetting;applySettings();
    bool hq=ModSounds_Has("MRIntW/HQ/ShotgunPumpDown");
    for(int mask=0;mask<8;++mask) {
        settings.fluidWeapons=mask&1;settings.fluidSounds=(mask>>1)&1;settings.fluidMonsters=(mask>>2)&1;applySettings();
        if(bool(P_FluidWeaponsEnabled())!=bool((mask&1)&&M_CheckParm((char*)"-fluidweapons")))I_Error((char*)"Weapon toggle failed");
        if(bool(P_FluidEnabled())!=bool((mask&4)&&M_CheckParm((char*)"-fluidmonsters")&&!demoplayback&&!demorecording&&!netgame))I_Error((char*)"Monster toggle failed");
        if(hq&&bool(ModSounds_Has("MRIntW/HQ/ShotgunPumpDown"))!=bool(mask&2))I_Error((char*)"SFX addon toggle failed");
        if(M_CheckParm((char*)"-modsounds")) {
            mod_sound_view sample;char error[512];
            const char *name=(hq&&(mask&2))?"MRIntW/HQ/PistolFire":"MRIntW/PistolFire";
            if(!ModSounds_Get(name,0,&sample,error,sizeof(error)))I_Error((char*)"Sound toggle decode: %s",error);
        }
    }
    settings.fluidWeapons=weapons;settings.fluidSounds=sounds;settings.fluidMonsters=monsters;applySettings();
    puts("Bundled mods: all eight live toggle combinations and sound-bank switching passed.");
}
extern "C" void I_TestSmoothActor(mobj_t *actor){
    float step=actor->info->speed/4.f;
    for(float fraction:{0.f,.25f,.5f,.75f,1.f}){
        float x,y,z;interpolatedPosition(*actor,fraction,x,y,z);
        if(std::abs(x-(units(actor->x)-step+step*fraction))>.0001f)
            I_Error((char*)"Monster render interpolation skipped a subframe");
    }
}
#endif
int I_Render3DWheelOpen(void){return wheelActive;}
void I_Render3DWheelKey(int down){
    if(down>0){if(gamestate!=GS_LEVEL||menuactive||paused||demoplayback||demorecording||netgame||players[consoleplayer].health<=0)return;wheelActive=true;wheelLevel=r_levelserial;wheelSelected=players[consoleplayer].readyweapon;wheelX=wheelY=0;}
    else {if(down==0&&wheelActive&&gamestate==GS_LEVEL&&!menuactive&&!paused&&wheelLevel==r_levelserial&&players[consoleplayer].health>0&&wheelOwned(wheelSelected)&&wheelSelected!=players[consoleplayer].readyweapon)players[consoleplayer].pendingweapon=(weapontype_t)wheelSelected;wheelActive=false;}
}
void I_Render3DWheelMove(float x,float y){if(!wheelActive)return;wheelX+=x*.35f;wheelY+=y*.35f;float radius=std::hypot(wheelX,wheelY*1.2f);if(radius>76){wheelX*=76/radius;wheelY*=76/radius;}if(radius>12){float angle=std::atan2(wheelY*1.2f,wheelX)+1.570796327f+3.141592654f/9;wheelSelected=int(std::floor(angle/(6.283185307f/9)+9))%9;}}
int I_Render3DIsAccelerated(void) {return settings.accelerated;}
int I_Render3DModernHUD(void) {return settings.accelerated&&settings.hudStyle!=0;}
void I_Render3DFullscreenChanged(int enabled) {settings.fullscreen=enabled!=0;}
void I_Render3DLook(float delta) {if(settings.look&&settings.accelerated&&!demoplayback)pitch=std::clamp(pitch-delta*0.002f,P_UseUZMovement()?-1.55334f:-1.2f,P_UseUZMovement()?1.55334f:1.2f);}
void I_Render3DToggleFlashlight(void) {if(settings.accelerated)flashlightOn=!flashlightOn;}
void I_Render3DScreenshot(void) {screenshotPending=true;}
const I_Render3DFrameProfile *I_Render3DLastFrame(void) {return &profile.frame;}
void I_Render3DProfileTeleported(void) {surfaceSelection.clear();surfaceLightTime=0;}
void I_Render3DScreenshotTo(const char *path) {profile.screenshotPath=path;screenshotPending=true;}



// Regression fixture invoked by the GPU smoke harness with a real WAD loaded.
extern "C" void I_TestCombatPresentation(void) {
    using namespace doom3d;
    Settings saved=settings;int oldtime=leveltime;
    settings.hitFlinch=settings.deathMotion=settings.deathDirector=settings.deathFalls=settings.wounds=1;
    mobj_t *player=players[consoleplayer].mo;
    const int types[]={MT_POSSESSED,MT_SHOTGUY,MT_CHAINGUY,MT_TROOP,MT_SERGEANT,MT_HEAD,MT_KNIGHT,MT_BRUISER,MT_UNDEAD,MT_VILE,MT_FATSO};
    int directed=0,procedural=0;
    for(int type:types){
        mobj_t *actor=P_SpawnMobj(player->x+96*FRACUNIT,player->y,ONFLOORZ,(mobjtype_t)type);
        watchBodies();int health=actor->health;
        P_DamageMobj(actor,player,player,5);++leveltime;watchBodies();
        ++leveltime;BodyPose pose;if(!bodyPose(*actor,0.5f,pose))I_Error((char*)"Hit reaction missing");
        if(actor->health!=health-5)I_Error((char*)"Presentation changed damage");
        P_DamageMobj(actor,player,player,actor->health+1);++leveltime;watchBodies();
        for(int tick=0;tick<8;++tick){
            DeathView view={actor->sprite,actor->frame,actor->angle,false,1,0,{}};
            if(deathView(*actor,0.5f,view)){
                ++directed;if(view.fall.active)++procedural;
                if(view.fall.rig){std::vector<Vertex> vertices;fallQuads(vertices,{},view.fall,0,0,0,1,0,false);if(vertices.empty())I_Error((char*)"Procedural fall has no geometry");}
            }
            ++leveltime;
        }
        P_RemoveMobj(actor);watchBodies();
    }
    if(!directed||!procedural)I_Error((char*)"Varied/procedural deaths not exercised");
    settings=saved;leveltime=oldtime;bodyMotionShutdown();
    printf("Combat presentation: real monster damage, flinches, varied deaths and procedural geometry passed.\n");
}

extern "C" void I_TestEffectsPreview(int t){
    using namespace doom3d;static mobj_t *actors[3]={};
    static int last=-1;if(t==last)return;last=t;players[consoleplayer].cheats|=CF_GODMODE;
    mobj_t *p=players[consoleplayer].mo;if(!p)return;
    float angle=(float)(p->angle*(2*doomPi/4294967296.0)),dx=std::cos(angle),dy=std::sin(angle);
    if(t==20){
        int types[]={MT_TROOP,MT_POSSESSED,MT_SHOTGUY};
        for(int i=0;i<3;++i){float side=(i-1)*48.f;
            actors[i]=P_SpawnMobj(p->x+(fixed_t)((dx*128-dy*side)*FRACUNIT),p->y+(fixed_t)((dy*128+dx*side)*FRACUNIT),ONFLOORZ,(mobjtype_t)types[i]);
            actors[i]->angle=p->angle+ANG180;actors[i]->reactiontime=1000;
        }
    }
    if(t==35)for(mobj_t *a:actors){
        bloodHit(a,a->x,a->y,a->z+32*FRACUNIT,(fixed_t)(dx*FRACUNIT),(fixed_t)(dy*FRACUNIT),5,0,p);
        P_DamageMobj(a,p,p,5);a->target=nullptr;a->momx=a->momy=0;
    }
    if(t==55)for(int i=1;i<3;++i){P_DamageMobj(actors[i],p,p,actors[i]->health+1);actors[i]->momx=actors[i]->momy=0;}
    if(t==40||t==60||t==68){
        static char path[1024];snprintf(path,sizeof(path),"%s/effects-%d.png",getenv("DOOM_EFFECTS_OUT"),t);I_Render3DScreenshotTo(path);
    }
    if(t>=80)I_Quit();
}
