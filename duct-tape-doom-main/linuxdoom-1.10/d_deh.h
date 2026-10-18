// DEHACKED and BEX patch support.

#ifndef __D_DEH__
#define __D_DEH__

#include "doomtype.h"

// Values a "Misc" block can change. The defaults are vanilla's.
extern int	deh_initial_health;
extern int	deh_initial_bullets;
extern int	deh_max_health;
extern int	deh_max_armor;
extern int	deh_green_armor_class;
extern int	deh_blue_armor_class;
extern int	deh_max_soulsphere;
extern int	deh_soulsphere_health;
extern int	deh_megasphere_health;
extern int	deh_god_mode_health;
extern int	deh_idfa_armor;
extern int	deh_idfa_armor_class;
extern int	deh_idkfa_armor;
extern int	deh_idkfa_armor_class;
extern int	deh_bfg_cells_per_shot;
extern boolean	deh_species_infighting;

// Applies DEHACKED lumps in WAD order, then files given with -deh.
// Call after the WADs are loaded and before R_Init and S_Init.
void	DEH_Init (void);

// Returns the patched replacement for a built-in string, or the
// string itself.
char*	DEH_String (const char* text);

#endif
