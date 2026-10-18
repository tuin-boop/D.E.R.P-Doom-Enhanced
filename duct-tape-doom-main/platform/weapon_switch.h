// Native playback of original Fluid Weapons Select/Deselect overlays.
#ifndef DOOM_WEAPON_SWITCH_H
#define DOOM_WEAPON_SWITCH_H
#include "fluid_weapons.h"
namespace doomweapon {
struct SwitchPose {int id;const char* sprite;Transform transform;float alpha;unsigned flags;};
struct SwitchFrame {unsigned first,count;};
struct SwitchClip {unsigned first,count;};
#include "weapon_switch_data.h"
class WeaponSwitch {
    int weapon=-1,previousPhase=0,phase=0,age=0,variant=0;
    unsigned choice=0x912b48a3;
public:
    void reset(){weapon=-1;previousPhase=phase=age=0;}
    // phase 0=ready/attack, 1=raising, 2=lowering. Settling can continue
    // after A_Raise, but must never cover a shot or auxiliary animation.
    void tick(int w,int requested,bool interrupt=false){
        bool changed=w!=weapon;
        if(changed){weapon=w;previousPhase=phase=0;}
        if(requested && (requested!=previousPhase||changed)){
            choice=choice*1664525u+1013904223u;
            variant=(choice>>16)%4;phase=requested;age=0;
        }else if(phase)++age;
        previousPhase=requested;
        if(w<0||w>8||interrupt||(phase==2&&requested!=2)||
           (phase==1&&age>=32))phase=0;
    }
    bool active()const{return phase!=0;}
    const SwitchClip& clip(bool bareHands=true)const{return switchClips[(weapon==0&&!bareHands?72:weapon*8)+variant*2+(phase==2)];}
    const SwitchFrame& frame(int t,bool bareHands=true)const {
        const auto&c=clip(bareHands);return switchFrames[c.first+std::clamp(t,0,int(c.count)-1)];
    }
    std::vector<doomfluid::Layer> layers(float fraction,bool bareHands)const{
        std::vector<doomfluid::Layer> result;if(!active())return result;
        const auto&now=frame(age,bareHands);const auto&old=frame(age-1,bareHands);
        for(unsigned i=0;i<now.count;++i){
            const auto&p=switchPoses[now.first+i];auto t=p.transform;float alpha=p.alpha;
            // Captured PSPF_INTERPOLATE is reference-engine bookkeeping: it
            // can be cleared while an overlay is still moving. Our playback
            // must interpolate continuous poses at the display frame rate,
            // rather than expose those 35-Hz steps to the player.
            if(age>0)for(unsigned j=0;j<old.count;++j){
                const auto&o=switchPoses[old.first+j];
                // Changed pivots/flips are explicit frame transitions.
                if(o.id==p.id&&(o.flags&3)==(p.flags&3)&&
                   o.transform.pivotX==t.pivotX&&o.transform.pivotY==t.pivotY){
                    t=interpolate(o.transform,t,fraction);
                    alpha=o.alpha+(p.alpha-o.alpha)*std::clamp(fraction,0.f,1.f);break;
                }
            }
            std::string sprite=p.sprite;
            result.push_back({sprite,t,false,alpha,bool(p.flags&8),bool(p.flags&1),bool(p.flags&2),false});
        }
        return result;
    }
};
}
#endif
