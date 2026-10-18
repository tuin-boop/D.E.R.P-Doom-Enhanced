/* The light thinkers' swings, for the 3D scene's flickering lamps
   (surface_lights.cpp). Read in C, so the C++ scene never includes
   p_spec.h, whose open and close enumerators clash with system headers. */
#include "p_local.h"
#include "light_swings.h"

void T_FireFlicker(fireflicker_t *flick);

void doom_light_swings(doom_swing_visit visit, void *data)
{
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        const sector_t *sector = NULL;
        int low = 0, high = 0, smooth = 0;
        if (th->function.acp1 == (actionf_p1)T_LightFlash)
        { lightflash_t *l = (lightflash_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; }
        else if (th->function.acp1 == (actionf_p1)T_StrobeFlash)
        { strobe_t *l = (strobe_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; }
        else if (th->function.acp1 == (actionf_p1)T_Glow)
        { glow_t *l = (glow_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; smooth = 1; }
        else if (th->function.acp1 == (actionf_p1)T_FireFlicker)
        { fireflicker_t *l = (fireflicker_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; smooth = 1; }
        if (sector && high > low) visit((int)(sector - sectors), low, high, smooth, data);
    }
}
