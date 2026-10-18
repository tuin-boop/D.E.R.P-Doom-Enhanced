/* Eye adaptation to the light around the player, and the flashlight
   tint and bounce from what it lights. */
#include "internal.h"

namespace doom3d {
static std::array<float,3> flashlightTint={},flashlightTargetTint={};
static uint64_t flashlightSampleTime;
static uint64_t flashlightTime;
static unsigned flashlightLevelSerial;
// The flashlight's bounce: a soft light where the beam lands, in the color of
// what it lights, so a bright spot on a wall spills onto the dark floor
// before it rather than standing beside it untouched.
struct Bounce {std::array<float,3> position,color;float normal[2],strength;};
static Bounce bounce={},targetBounce={};

// Eye adaptation: what the eye has settled to follows the light around and
// ahead of the player, quickly toward brighter light and slowly toward
// darker. Exposure is the square root of target over settled light, in
// sixteenths like Doom's light levels, updated once per tic. Each sector's
// light is held at its recent peak so flickering and strobing sectors don't
// pump the exposure.
static float adaptedLight=-1,exposure=1;static int adaptTic=-1;static unsigned adaptLevelSerial;
static std::vector<std::pair<float,int>> steadyLights;
static float steadyLight(const sector_t *sector) {
    auto &held=steadyLights[sector-sectors];
    float now=lighting(sector),value=held.second<0?now:std::max(now,held.first-(leveltime-held.second)*0.01f);
    held={value,leveltime};
    return value;
}
static float viewLight(const Uniforms &camera) {
    auto lightAt=[&](float x,float y,const sector_t *sector) {return sunLightAt(x,y,sector,seamLightAt(x,y,sector,steadyLight(sector)));};
    float own=lightAt(camera.eye[0],camera.eye[1],players[displayplayer].mo->subsector->sector);
    float fx=camera.forward[0],fy=camera.forward[1],flat=std::hypot(fx,fy);
    if(flat<0.2f)return own;
    fx/=flat;fy/=flat;
    WallHit hit;
    float reach=firstSolidWall(camera.eye[0],camera.eye[1],camera.eye[2],fx,fy,camera.forward[2]/flat,512,hit)?hit.t:512;
    float sum=0;int count=0;
    for(float d:{64.0f,160.0f,320.0f}) {
        float t=std::min(d,reach-8);if(t<8)break;
        float px=camera.eye[0]+fx*t,py=camera.eye[1]+fy*t;
        sum+=lightAt(px,py,R_PointInSubsector((fixed_t)(px*FRACUNIT),(fixed_t)(py*FRACUNIT))->sector);++count;
    }
    return count?own*0.4f+sum/count*0.6f:own;
}
void updateExposure(Uniforms &camera) {
    if(!settings.eyeAdaptation||camera.effects[2]!=0||!players[displayplayer].mo) {adaptedLight=-1;exposure=1;return;}
    if(adaptLevelSerial!=r_levelserial||steadyLights.size()!=(size_t)numsectors) {
        adaptedLight=-1;adaptLevelSerial=r_levelserial;steadyLights.assign(numsectors,{0.0f,-1});
    }
    if(leveltime!=adaptTic) {
        float target=viewLight(camera);
        if(adaptedLight<0)adaptedLight=target;
        else {
            float dt=std::clamp((leveltime-adaptTic)/(float)TICRATE,0.0f,0.5f),tau=target>adaptedLight?0.5f:1.6f;
            adaptedLight+=(target-adaptedLight)*(1-std::exp(-dt/tau));
        }
        adaptTic=leveltime;
        float ratio=std::sqrt(target/std::max(adaptedLight,0.05f));
        exposure=1+std::round((std::clamp(ratio,0.6f,1.5f)-1)*16)/16;
    }
    camera.fx[0]=exposure;
}
// One target-color ray at 30 Hz supplies a smooth, subtle ambient tint.
// It creates no virtual lights, portal blocker lists, or per-pixel bounce work.
void updateFlashlightTint(Uniforms &camera) {
    uint64_t now=SDL_GetTicksNS();
    if(flashlightLevelSerial!=r_levelserial) {
        flashlightTint={};flashlightTargetTint={};flashlightTime=0;flashlightSampleTime=0;
        bounce={};targetBounce={};flashlightLevelSerial=r_levelserial;
    }
    if(!flashlightOn||camera.effects[2]!=0) {flashlightTargetTint={};targetBounce.strength=0;}
    else if(!flashlightSampleTime||now-flashlightSampleTime>=33333333) {
        flashlightSampleTime=now;
        const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        using Vec=std::array<float,3>;
        auto dot=[](Vec a,Vec b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        auto cross=[](Vec a,Vec b)->Vec{return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        Vec direction;
        for(int c=0;c<3;++c) direction[c]=camera.forward[c];
        float length=std::sqrt(dot(direction,direction));for(float &c:direction)c/=length;
        float nearest=900,nearestSurface=900;Vec reflected={};Bounce hit={};
        for(const auto &batch:batches) {
            const auto &image=images.at(batch.first);const auto &vertices=batch.second;
            for(size_t i=0;i+2<vertices.size();i+=3) {
                const auto &a=vertices[i],&b=vertices[i+1],&c=vertices[i+2];
                Vec ab={b.x-a.x,b.y-a.y,b.z-a.z},ac={c.x-a.x,c.y-a.y,c.z-a.z};
                Vec h=cross(direction,ac);float det=dot(ab,h);if(std::abs(det)<0.0001f)continue;
                Vec origin={camera.eye[0]-a.x,camera.eye[1]-a.y,camera.eye[2]-a.z};
                float u=dot(origin,h)/det;if(u<0||u>1)continue;
                Vec q=cross(origin,ab);float v=dot(direction,q)/det;if(v<0||u+v>1)continue;
                // The tint takes whatever is nearest; the bounce only map
                // surfaces, so a monster in the beam isn't lit flat from a
                // light on its own face.
                bool sprite=(a.mode&MODE_SPRITE)!=0;
                float distance=dot(ac,q)/det;if(distance<1||(distance>=nearest&&(sprite||distance>=nearestSurface)))continue;
                int tx=(int)std::floor(a.u+u*(b.u-a.u)+v*(c.u-a.u));
                int ty=(int)std::floor(a.v+u*(b.v-a.v)+v*(c.v-a.v));
                if(sprite) {if(tx<0||ty<0||tx>=image.width||ty>=image.height)continue;}
                else {tx=(tx%image.width+image.width)%image.width;ty=(ty%image.height+image.height)%image.height;}
                size_t pixel=((size_t)ty*image.width+tx)*2;
                if(!image.pixels[pixel+1])continue;
                // Average a small patch of artwork so a single dark mortar
                // pixel doesn't abruptly change the tint while aiming.
                Vec albedo={};float samples=0;
                for(int py=-2;py<=2;++py)for(int px=-2;px<=2;++px) {
                    int x=(tx+px+image.width)%image.width,y=(ty+py+image.height)%image.height;
                    size_t index=((size_t)y*image.width+x)*2;
                    if(!image.pixels[index+1])continue;
                    const byte *rgb=palette+image.pixels[index]*3;
                    for(int c=0;c<3;++c)albedo[c]+=rgb[c]/255.0f;
                    ++samples;
                }
                for(float &c:albedo)c/=std::max(1.0f,samples);
                if(distance<nearest) {
                    nearest=distance;
                    float gain=0.30f*std::pow(std::max(0.0f,1-distance/900),2.0f)*settings.flashlightTintGain;
                    for(int c=0;c<3;++c)reflected[c]=albedo[c]*gain;
                }
                if(sprite||distance>=nearestSurface)continue;
                nearestSurface=distance;
                // The bounce: as bright as the beam there (its falloff in
                // collectFlashes) times how much the artwork reflects.
                Vec normal=cross(ab,ac);float n=std::sqrt(dot(normal,normal));
                if(n<0.0001f)continue;
                for(float &c:normal)c/=n;
                if(dot(normal,direction)>0)for(float &c:normal)c=-c;
                float peak=std::max({albedo[0],albedo[1],albedo[2]}),beam=std::max(0.0f,1-distance/900);
                hit.strength=peak>0?0.6f*1.9f*beam*beam*(3-2*beam)*(0.299f*albedo[0]+0.587f*albedo[1]+0.114f*albedo[2]):0;
                for(int c=0;c<3;++c) {hit.position[c]=camera.eye[c]+direction[c]*distance;hit.color[c]=peak>0?albedo[c]/peak:0;}
                // Walls: a panel 4 units out shining into the room (doom_panel_emission);
                // floors, ceilings and anything tilted: a point a little off it.
                float flat=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]);
                bool wall=std::abs(normal[2])<0.1f;
                hit.normal[0]=wall?normal[0]/flat:0;hit.normal[1]=wall?normal[1]/flat:0;
                for(int c=0;c<3;++c)hit.position[c]+=normal[c]*(wall?4.0f:16.0f);
            }
        }
        flashlightTargetTint=reflected;targetBounce=hit;
    }
    float seconds=flashlightTime?std::min(0.1f,(now-flashlightTime)/1e9f):1.0f/60;
    flashlightTime=now;float blend=1-std::exp(-seconds/0.16f);
    for(int c=0;c<3;++c) {
        flashlightTint[c]+=(flashlightTargetTint[c]-flashlightTint[c])*blend;
        camera.flashlightTint[c]=camera.effects[2]==0?flashlightTint[c]:0;
    }
    // Moving across one surface the bounce follows; jumping to another it
    // fades in there instead of sliding through the room.
    float dx=targetBounce.position[0]-bounce.position[0],dy=targetBounce.position[1]-bounce.position[1],dz=targetBounce.position[2]-bounce.position[2];
    bool jump=dx*dx+dy*dy+dz*dz>48*48||targetBounce.normal[0]!=bounce.normal[0]||targetBounce.normal[1]!=bounce.normal[1];
    if(targetBounce.strength>0&&(bounce.strength<=0.001f||jump)) {bounce=targetBounce;bounce.strength=0;}
    bounce.strength+=(targetBounce.strength-bounce.strength)*blend;
    for(int c=0;c<3;++c) {
        bounce.position[c]+=(targetBounce.position[c]-bounce.position[c])*blend;
        bounce.color[c]+=(targetBounce.color[c]-bounce.color[c])*blend;
    }
}
bool flashlightBounce(float position[3],float normal[2],std::array<float,3> &color,float &strength) {
    if(bounce.strength<0.01f) return false;
    for(int c=0;c<3;++c)position[c]=bounce.position[c];
    normal[0]=bounce.normal[0];normal[1]=bounce.normal[1];color=bounce.color;strength=bounce.strength;
    return true;
}
void exposureShutdown() {
    flashlightTint={};flashlightTargetTint={};flashlightTime=0;flashlightSampleTime=0;
    bounce={};targetBounce={};
    steadyLights.clear();adaptedLight=-1;exposure=1;
}
} // namespace doom3d
