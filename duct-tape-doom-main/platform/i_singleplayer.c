#include <stdlib.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_net.h"
#include "m_argv.h"

void I_InitNetwork(void)
{
    if (M_CheckParm("-net"))
        I_Error("This build supports single-player games");
    doomcom = calloc(1, sizeof(*doomcom));
    if (!doomcom)
        I_Error("Could not allocate network state");
    doomcom->id = DOOMCOM_ID;
    doomcom->ticdup = 1;
    doomcom->numplayers = doomcom->numnodes = 1;
    netgame = false;
}
void I_NetCmd(void) { I_Error("Multiplayer is not available in this build"); }
