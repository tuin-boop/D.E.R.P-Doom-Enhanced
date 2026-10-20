// Exercise the real wound capture and drawing path without a WAD or GPU.
// The synthetic animation moves a textured body part inside a fixed outline.
#include "../platform/scene3d/internal.h"
#include <cstdlib>

static void check(bool ok,const char *message) {
    if(!ok) {std::fprintf(stderr,"%s\n",message);std::exit(1);}
}

extern "C" {
state_t states[NUMSTATES]={};
int firstspritelump=100;
int numsprites=NUMSPRITES;
int leveltime=0;
spritedef_t *sprites=nullptr;

}

namespace doom3d {
Settings settings;
static std::array<Image,3> spriteImages;
static unsigned bodyQuadCalls=0;
const std::vector<byte> &bloodPalette() {
    static const std::vector<byte> shades={16,32,48,64,80,96,112,128,144};
    return shades;
}
Image &lumpImage(int lump,bool flat) {
    check(!flat&&lump>=firstspritelump&&lump<firstspritelump+(int)spriteImages.size(),
          "Wounds must capture a valid sprite patch");
    return spriteImages[(size_t)(lump-firstspritelump)];
}
void quad(std::vector<Vertex> &out,Vertex a,Vertex b,Vertex c,Vertex d) {
    out.insert(out.end(),{a,b,c,a,c,d});
}
// A small deforming surface with known UVs. Capture must use the same
// triangle path as a drawn body, including lateral bend, depth and lift.
void bodyQuads(std::vector<Vertex> &out,const BodyPose &pose,const Vertex &base,float x,float y,float feet,float rx,float ry,
               float left,float right,int imageTop,int rows,float u0,float u1,const std::vector<byte> *holes) {
    check(!holes,"Impact capture must intersect the uncut body surface");
    ++bodyQuadCalls;
    auto corner=[&](float across,float u,float v) {
        float height=imageTop-v,shift=pose.lean[0]*height,ahead=pose.whip[0]*height;
        Vertex vertex=base;
        vertex.x=x+rx*(across+shift)-ry*ahead;
        vertex.y=y+ry*(across+shift)+rx*ahead;
        vertex.z=feet+height*(1-pose.squash);
        vertex.u=u;vertex.v=v;
        return vertex;
    };
    for(int row=0;row<rows;++row)
        quad(out,corner(left,u0,(float)row),corner(right,u1,(float)row),
             corner(right,u1,(float)row+1),corner(left,u0,(float)row+1));
}
}

using namespace doom3d;
static spritedef_t spriteDefs[NUMSPRITES]={};
static spriteframe_t frames[3]={};
static mobjinfo_t monsterInfo={};

static fixed_t fixed(float value) {return (fixed_t)std::lround(value*FRACUNIT);}

static void paint(Image &image,int x,int y,byte color) {
    size_t i=((size_t)y*image.width+x)*2;
    image.pixels[i]=color;image.pixels[i+1]=255;
}
static void mark(Image &image,int x,int y) {
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
        paint(image,x+dx,y+dy,(byte)(70+(dy+2)*5+dx+2));
}
static Image image(int markX,int markY,int width=36,int height=46,int left=18,int top=46) {
    Image out;
    out.width=width;out.height=height;out.left=left;out.top=top;
    out.pixels.assign((size_t)out.width*out.height*2,0);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x)paint(out,x,y,20);
    mark(out,markX,markY);
    return out;
}
static mobj_t reset(bool flipped=false) {
    woundsShutdown();
    settings.wounds=1;
    bodyQuadCalls=0;
    leveltime=0;
    spriteImages[0]=image(24,20);
    spriteImages[1]=image(27,23);
    spriteImages[2]=image(21,17);
    for(int frame=0;frame<3;++frame) {
        frames[frame]={};
        for(int rotation=0;rotation<8;++rotation) {
            frames[frame].lump[rotation]=(short)frame;
            frames[frame].flip[rotation]=flipped;
        }
    }
    spriteDefs[SPR_POSS]={3,frames};
    sprites=spriteDefs;
    states[1].sprite=SPR_POSS;states[1].frame=0;
    monsterInfo={};monsterInfo.flags=MF_COUNTKILL;
    monsterInfo.spawnstate=(statenum_t)1;
    mobj_t monster={};
    monster.x=fixed(100);monster.y=fixed(200);monster.z=fixed(16);
    monster.height=fixed(46);monster.radius=fixed(16);
    monster.sprite=SPR_POSS;monster.frame=0;monster.angle=0;
    monster.info=&monsterInfo;monster.flags=MF_COUNTKILL;monster.health=100;
    monster.type=MT_POSSESSED;
    return monster;
}
static void hit(mobj_t &monster,float x,float y,float z,float dx,float dy,float slope=0,const mobj_t *shooter=nullptr) {
    woundHit(&monster,fixed(x),fixed(y),fixed(z),fixed(dx),fixed(dy),fixed(slope),1,shooter);
}
static std::vector<byte> mask(const mobj_t &monster,const Image &im,bool flip,float rx,float ry) {
    std::vector<byte> out;
    check(woundMask(monster,im,flip,rx,ry,out),"A shot viewed along its path must leave a wound");
    return out;
}
static void at(const std::vector<byte> &actual,const Image &im,float u,float v,const char *message) {
    check(actual.size()==(size_t)im.width*im.height,"The wound mask must cover the drawn patch");
    for(int y=0;y<im.height;++y)for(int x=0;x<im.width;++x) {
        float dx=x+0.5f-u,dy=y+0.5f-v;
        size_t i=(size_t)y*im.width+x;
        byte state=actual[i];bool opaque=im.pixels[i*2+1]!=0;
        check(state<=5,"Wound masks must contain only defined materials");
        check((state==1)==(opaque&&std::hypot(dx,dy)<=0.8f),message);
        check(opaque||state==0,"Blood must remain on opaque body texels");
        if(state)check(std::abs(dx)<=0.8f+2.6f&&dy>=-(0.8f+2.6f)&&dy<=0.8f+4.1f,
                       "Blood and its short smear must stay close to the impact");
    }
}
static void translated(const std::vector<byte> &actual,const std::vector<byte> &before,int width,int height,int dx,int dy) {
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        int sx=x-dx,sy=y-dy;
        byte expected=sx>=0&&sx<width&&sy>=0&&sy<height?before[(size_t)sy*width+sx]:0;
        check(actual[(size_t)y*width+x]==expected,
              "The complete blood pattern must follow its animated body part without changing");
    }
}
static void woundMaterials() {
    Vertex base={};base.mode=MODE_SPRITE|MODE_RELIT|MODE_GLOWING_EYES;
    base.lampTint=0xff92817au;
    // One row contains every material, including an intact body texel and
    // a hole. These pass through the same run drawing as an animated body.
    const std::vector<byte> states={0,1,2,3,4,5};
    std::vector<Vertex> vertices;
    settings.bloodShine=1;
    maskedRow(vertices,states,(int)states.size(),0,false,[&](int edge,bool bottom) {
        Vertex v=base;v.u=(float)edge;v.v=bottom?1.0f:0.0f;return v;
    });
    check(vertices.size()==30,"Wound drawing must skip holes and preserve the other materials");
    unsigned darkest=255,brightest=0;
    for(size_t i=0;i<vertices.size();++i) {
        const Vertex &v=vertices[i];
        int state=(int)(i/6);if(state)++state;
        check(v.lampTint==base.lampTint,"Blood must retain the body's scene lighting tint");
        if(!state) {check(v.mode==base.mode,"Intact body texels must retain their material");continue;}
        check((v.mode&MODE_WOUND)!=0&&(v.mode&MODE_GLOWING_EYES)==0,
              "Blood texels must use their blood material without glowing as eyes");
        unsigned color=(v.mode>>MODE_WOUND_PALETTE_SHIFT)&255u;
        check(std::find(bloodPalette().begin(),bloodPalette().end(),color)!=bloodPalette().end(),
              "Blood colors must come from the game's blood palette");
        if(state==2)darkest=color;if(state==4)brightest=color;
        check(((v.mode&MODE_WOUND_STAIN)!=0)==(state==5),
              "Only the outer stain must blend blood into the body paint");
        check(((v.mode&MODE_WET_SPRITE)!=0)==(state==3||state==4),
              "Only the red rim and bright lip must receive wet blood highlights");
    }
    check(brightest>darkest,"The bright blood lip must use a brighter shade than its recessed edge");
    Vertex repainted=base;
    woundVertex(repainted,5);woundVertex(repainted,3);
    check((repainted.mode&MODE_WOUND_STAIN)==0&&(repainted.mode&MODE_WET_SPRITE)!=0,
          "Repainting a stain as a red rim must replace its previous material");
    settings.bloodShine=0;
    for(byte state=2;state<=5;++state) {
        Vertex v=base;v.mode|=MODE_WET_SPRITE;
        woundVertex(v,state);
        check(!(v.mode&MODE_WET_SPRITE),"Disabling blood shine must remove every wound highlight");
    }
}

int main() {
    woundMaterials();
    mobj_t monster=reset();
    // The callback is the blood/puff point before the cylinder. Project it
    // forward onto the sprite plane at x=100, keeping the lateral impact.
    hit(monster,74,193.5f,41.5f,1,0);
    auto bloody=mask(monster,spriteImages[0],false,0,-1);
    at(bloody,spriteImages[0],24.5f,20.5f,
       "Front shots must cut the aimed texel rather than the body center");
    bool red=false,stain=false,trail=false;
    for(int y=0;y<46;++y)for(int x=0;x<36;++x) {
        byte state=bloody[(size_t)y*36+x];
        red=red||state==3||state==4;stain=stain||state==5;
        trail=trail||(state>=3&&y-20>2.5f&&std::abs(x-24)<=1);
    }
    check(red&&stain,"A puncture must include red blood and a surrounding stain");
    check(trail,"A bloody puncture must leave a short downward smear");
    auto mirrored=mask(monster,spriteImages[0],true,0,-1);
    for(int y=0;y<46;++y)for(int x=0;x<36;++x)
        check(bloody[(size_t)y*36+x]==mirrored[(size_t)y*36+35-x],
              "Mirroring the body must mirror the entire blood pattern with it");
    spriteImages[0].pixels[((size_t)22*36+24)*2+1]=0;
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "Clipping a blood mark to transparent artwork must preserve the puncture");
    std::vector<byte> closed;
    check(!woundMask(monster,spriteImages[0],false,1,0,closed),
          "A tunnel viewed perpendicular to the shot must close");

    monster=reset();
    hit(monster,74,193.75f,41.25f,1,0);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.25f,20.75f,
       "The original hit frame must preserve the impact's fractional texel position");

    monster=reset();
    hit(monster,106.5f,174,41.5f,0,1);
    at(mask(monster,spriteImages[0],false,1,0),spriteImages[0],24.5f,20.5f,
       "Side shots must use the shot's sprite plane and remain visible from that view");

    monster=reset();
    hit(monster,74,193.5f,35,1,0,0.25f);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "The shot slope must advance the puff height to the body plane");

    // Correspondence always starts at the impact art, so returning to a
    // previous animation frame cannot accumulate drift.
    monster=reset();
    hit(monster,74,193.5f,41.5f,1,0);
    auto original=mask(monster,spriteImages[0],false,0,-1);
    monster.frame=1;
    auto animated=mask(monster,spriteImages[1],false,0,-1);
    at(animated,spriteImages[1],27.5f,23.5f,
       "An animated body part must carry the wound to its new texel");
    translated(animated,original,36,46,3,3);
    monster.frame=2;
    animated=mask(monster,spriteImages[2],false,0,-1);
    at(animated,spriteImages[2],21.5f,17.5f,
       "Another animation frame must map directly from the original impact");
    translated(animated,original,36,46,-3,-3);
    monster.frame=0;
    leveltime+=37;
    check(mask(monster,spriteImages[0],false,0,-1)==original,
          "Blood must not flicker with time or change after an animation cycle");
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "Returning to the hit frame must restore its exact original anchor");
    monster.x+=fixed(30);monster.y-=fixed(11);monster.z+=fixed(5);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "Walking must keep the wound attached to the monster");
    monster.angle=ANG90;
    at(mask(monster,spriteImages[0],false,1,0),spriteImages[0],24.5f,20.5f,
       "Turning must rotate the wound's tunnel with the monster");

    monster=reset();
    hit(monster,74,193.5f,41.5f,1,0);
    spriteImages[1]=image(30,24,42,50,24,50);
    monster.frame=1;
    at(mask(monster,spriteImages[1],false,0,-1),spriteImages[1],30.5f,24.5f,
       "Animation patches with different sizes and offsets must retain the attached artwork");

    monster=reset(true);
    spriteImages[0]=image(11,20);spriteImages[1]=image(14,23);
    hit(monster,74,193.5f,41.5f,1,0);
    at(mask(monster,spriteImages[0],true,0,-1),spriteImages[0],11.5f,20.5f,
       "Mirrored impact frames must anchor in the authored texture coordinates");
    monster.frame=1;
    at(mask(monster,spriteImages[1],true,0,-1),spriteImages[1],14.5f,23.5f,
       "Mirrored animations must carry the same authored wound patch");

    forgetWounds(&monster);
    check(!woundMask(monster,spriteImages[1],true,0,-1,closed),
          "Removing a monster must discard its wounds");
    hit(monster,74,191.5f,36.5f,1,0);
    at(mask(monster,spriteImages[1],true,0,-1),spriteImages[1],9.5f,25.5f,
       "Reusing a removed monster's address must not inherit its old wounds");
    monster=reset();hit(monster,74,193.5f,41.5f,1,0);
    woundsShutdown();
    check(!woundMask(monster,spriteImages[0],false,0,-1,closed),
          "Scene shutdown must discard all wounds");
    settings.wounds=0;
    hit(monster,74,193.5f,41.5f,1,0);
    settings.wounds=1;
    check(!woundMask(monster,spriteImages[0],false,0,-1,closed),
          "Disabled wounds must not record later-visible impacts");

    // The game can already have moved to a new tic or sprite after the
    // frame the player aimed at. Capture the remembered rendered surface.
    mobj_t viewer={};
    viewer.x=fixed(0);viewer.y=fixed(194.5f);viewer.angle=0;
    monster=reset();
    woundView(&monster,&viewer,spriteImages[1],false,104,204,20,68,22,0,-1);
    hit(monster,74,194.5f,44.5f,1,0,0,&viewer);
    at(mask(monster,spriteImages[1],false,0,-1),spriteImages[1],27.5f,23.5f,
       "Impact capture must use the remembered image, interpolated position and drawn vertical bounds");
    check(bodyQuadCalls==0,"An undeformed remembered billboard needs no body reconstruction");

    monster=reset();
    BodyPose pose={};pose.lean[0]=0.2f;pose.whip[0]=0.1f;pose.squash=0.2f;pose.lift=3;
    viewer.y=fixed(190);
    woundView(&monster,&viewer,spriteImages[1],false,104,204,23,69,23,0,-1,&pose);
    pose={}; // A remembered surface owns its pose rather than this pointer.
    hit(monster,74,190,41,1,0,0,&viewer);
    at(mask(monster,spriteImages[1],false,0,-1),spriteImages[1],27.5f,23.5f,
       "A flinching surface must map its visible triangle back to the struck source texel");
    check(bodyQuadCalls==1,"A posed hit must reconstruct the remembered body once on demand");

    monster=reset();viewer.y=fixed(194.5f);
    woundView(&monster,&viewer,spriteImages[1],false,70,204,20,68,22,0,-1);
    hit(monster,74,194.5f,44.5f,1,0,0,&viewer);
    at(mask(monster,spriteImages[1],false,0,-1),spriteImages[1],27.5f,23.5f,
       "A visible surface in front of the puff must still be struck by the shooter's ray");

    monster=reset();viewer.y=fixed(200.5f);
    woundView(&monster,&viewer,spriteImages[1],true,104,204,20,68,22,0,-1);
    hit(monster,74,200.5f,44.5f,1,0,0,&viewer);
    at(mask(monster,spriteImages[1],true,0,-1),spriteImages[1],14.5f,23.5f,
       "Remembered mirrored surfaces must invert their UVs when capturing the hit");

    monster=reset();viewer.y=fixed(194.5f);
    spriteImages[1].pixels[((size_t)23*spriteImages[1].width+27)*2+1]=0;
    woundView(&monster,&viewer,spriteImages[1],false,104,204,20,68,22,0,-1);
    hit(monster,74,194.5f,44.5f,1,0,0,&viewer);
    check(!woundMask(monster,spriteImages[0],false,0,-1,closed),
          "A ray through transparent remembered art must not cut a different body texel");

    monster=reset();
    mobj_t otherViewer=viewer;
    woundView(&monster,&viewer,spriteImages[1],false,104,204,20,68,22,0,-1);
    hit(monster,74,193.5f,41.5f,1,0,0,&otherViewer);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "Another shooter's hit must not use the local viewer's remembered surface");

    monster=reset();
    woundView(&monster,&viewer,spriteImages[1],false,104,204,20,68,22,0,-1);
    leveltime+=3;
    hit(monster,74,193.5f,41.5f,1,0,0,&viewer);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "A stale remembered view must fall back to the current body");

    monster=reset();
    woundView(&monster,&viewer,spriteImages[1],false,104,204,20,68,22,0,-1);
    forgetWounds(&monster);
    hit(monster,74,193.5f,41.5f,1,0,0,&viewer);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "Removing a monster must also discard its remembered rendered surface");

    monster=reset();
    player_t player={};viewer.player=&player;viewer.y=fixed(143.5f);
    hit(monster,74,180.5f,41.5f,1,0.5f,0,&viewer);
    at(mask(monster,spriteImages[0],false,0,-1),spriteImages[0],24.5f,20.5f,
       "A spread pellet must intersect the player's camera plane rather than rotate its own billboard");

    // The sprite rotation comes from the viewer-to-body center bearing.
    // Here the center is at 22 degrees and the actual ray is at 24, on
    // opposite sides of the 22.5-degree sprite-rotation boundary.
    monster=reset();
    frames[0].rotate=true;
    for(int rotation=0;rotation<8;++rotation)frames[0].lump[rotation]=2;
    frames[0].lump[4]=0;
    std::fill(spriteImages[2].pixels.begin(),spriteImages[2].pixels.end(),0);
    float rayAngle=24*doomPi/180,centerAngle=22*doomPi/180;
    float dx=std::cos(rayAngle),dy=std::sin(rayAngle);
    float viewerY=200-100*std::tan(centerAngle);
    viewer.y=fixed(viewerY);
    viewer.angle=(angle_t)std::llround(24*(4294967296.0/360));
    float distance=100*dx+(200-viewerY)*dy;
    float impactX=distance*dx,impactY=viewerY+distance*dy;
    float across=(impactX-100)*dy-(impactY-200)*dx;
    hit(monster,impactX-26*dx,impactY-26*dy,41.5f,dx,dy,0,&viewer);
    at(mask(monster,spriteImages[0],false,dy,-dx),spriteImages[0],18+across,20.5f,
       "Off-center rays must use the rotation selected from the player's position");

    // Nearby pellets form one larger attached hole centered between hits,
    // rather than growing a hole that never reaches the second impact.
    monster=reset();
    woundHit(&monster,fixed(74),fixed(193.5f),fixed(41.5f),fixed(1),0,0,15);
    woundHit(&monster,fixed(74),fixed(190.5f),fixed(41.5f),fixed(1),0,0,15);
    auto merged=mask(monster,spriteImages[0],false,0,-1);
    check(merged[20*36+24]==1&&merged[20*36+27]==1,
          "A merged pellet hole must include both original impact texels");
    leveltime+=37;
    check(mask(monster,spriteImages[0],false,0,-1)==merged,
          "A merged wound's blood pattern must remain stable after the shot");
    return 0;
}
