/* Weapon overlay transforms, independent of simulation and GPU state.
 * Coordinates use Doom's 320x200 weapon canvas. Angles are degrees.
 * This is the rendering foundation for the Fluid Weapons port, not a
 * ZScript interpreter. New overlays snap until they have a previous tick. */
#ifndef DOOM_WEAPON_TRANSFORM_H
#define DOOM_WEAPON_TRANSFORM_H
#include <algorithm>
#include <array>
#include <cmath>

namespace doomweapon {
struct Transform {
    float x=0,y=0,scaleX=1,scaleY=1,rotation=0,pivotX=0,pivotY=0;
};
inline bool valid(const Transform &t) {
    const float fields[]={t.x,t.y,t.scaleX,t.scaleY,t.rotation,t.pivotX,t.pivotY};
    for(float f:fields)if(!std::isfinite(f))return false;
    return std::abs(t.x)<=65536 && std::abs(t.y)<=65536 &&
        std::abs(t.rotation)<=360000 &&
        std::abs(t.scaleX)<=64 && std::abs(t.scaleY)<=64 &&
        std::abs(t.pivotX)<=65536 && std::abs(t.pivotY)<=65536;
}
inline Transform interpolate(const Transform &old,const Transform &now,float fraction) {
    float f=std::isfinite(fraction)?std::clamp(fraction,0.0f,1.0f):1.0f;
    auto mix=[f](float a,float b){return a+(b-a)*f;};
    return {mix(old.x,now.x),mix(old.y,now.y),mix(old.scaleX,now.scaleX),mix(old.scaleY,now.scaleY),
        old.rotation+std::remainder(now.rotation-old.rotation,360.0f)*f,
        mix(old.pivotX,now.pivotX),mix(old.pivotY,now.pivotY)};
}
inline std::array<float,2> point(const Transform &t,float x,float y) {
    float angle=t.rotation*0.01745329251994329577f,c=std::cos(angle),s=std::sin(angle);
    // PSprite rotation is counter-clockwise in screen space, followed by
    // axis scaling. Rotate in display pixels (Doom pixels are 1.2 high).
    // Reference: GZDoom src/rendering/hwrenderer/scene/hw_weapon.cpp.
    float px=x-t.pivotX,py=(y-t.pivotY)*1.2f;
    return {t.x+t.pivotX+t.scaleX*(px*c+py*s),
            t.y+t.pivotY+t.scaleY*(-px*s+py*c)/1.2f};
}
class Motion {
    Transform previous{},current{};
    bool initialized=false;
public:
    void reset() {initialized=false;previous=current=Transform{};}
    void beforeTick() {previous=current;}
    bool set(const Transform &value,bool interpolateChange=true) {
        if(!valid(value))return false;
        current=value;
        if(!initialized||!interpolateChange)previous=current;
        initialized=true;return true;
    }
    Transform sample(float fraction) const {return interpolate(previous,current,fraction);}
};
}
#endif
