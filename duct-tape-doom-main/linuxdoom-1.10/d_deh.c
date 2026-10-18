// DEHACKED and BEX patch support.
//
// Applies vanilla DeHackEd v3 patches (Thing, Frame, Pointer, Sound,
// Ammo, Weapon, Cheat, Misc and Text blocks), Boom's BEX [STRINGS],
// [PARS] and [CODEPTR] sections, and [SPRITES], [SOUNDS] and [MUSIC]
// renames. MBF and MBF21 additions need engine features this port
// lacks; they are reported and skipped.

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "dstrings.h"
#include "d_deh.h"
#include "d_items.h"
#include "info.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_cheat.h"
#include "p_mobj.h"
#include "sounds.h"
#include "w_wad.h"

int	deh_initial_health = 100;
int	deh_initial_bullets = 50;
int	deh_max_health = 200;
int	deh_max_armor = 200;
int	deh_green_armor_class = 1;
int	deh_blue_armor_class = 2;
int	deh_max_soulsphere = 200;
int	deh_soulsphere_health = 100;
int	deh_megasphere_health = 200;
int	deh_god_mode_health = 100;
int	deh_idfa_armor = 200;
int	deh_idfa_armor_class = 2;
int	deh_idkfa_armor = 200;
int	deh_idkfa_armor_class = 2;
int	deh_bfg_cells_per_shot = 40;
boolean	deh_species_infighting = false;

extern int	maxammo[NUMAMMO];
extern int	clipammo[NUMAMMO];
extern int	pars[4][10];
extern int	cpars[32];
extern cheatseq_t cheat_mus, cheat_choppers, cheat_god, cheat_ammo;
extern cheatseq_t cheat_ammonokey, cheat_noclip, cheat_commercial_noclip;
extern cheatseq_t cheat_powerup[7], cheat_clev, cheat_mypos, cheat_amap;

// Declared without prototypes, as in info.c.
void A_Light0();
void A_WeaponReady();
void A_Lower();
void A_Raise();
void A_Punch();
void A_ReFire();
void A_FirePistol();
void A_Light1();
void A_FireShotgun();
void A_Light2();
void A_FireShotgun2();
void A_CheckReload();
void A_OpenShotgun2();
void A_LoadShotgun2();
void A_CloseShotgun2();
void A_FireCGun();
void A_GunFlash();
void A_FireMissile();
void A_Saw();
void A_FirePlasma();
void A_BFGsound();
void A_FireBFG();
void A_BFGSpray();
void A_Explode();
void A_Pain();
void A_PlayerScream();
void A_Fall();
void A_XScream();
void A_Look();
void A_Chase();
void A_FaceTarget();
void A_PosAttack();
void A_Scream();
void A_SPosAttack();
void A_VileChase();
void A_VileStart();
void A_VileTarget();
void A_VileAttack();
void A_StartFire();
void A_Fire();
void A_FireCrackle();
void A_Tracer();
void A_SkelWhoosh();
void A_SkelFist();
void A_SkelMissile();
void A_FatRaise();
void A_FatAttack1();
void A_FatAttack2();
void A_FatAttack3();
void A_BossDeath();
void A_CPosAttack();
void A_CPosRefire();
void A_TroopAttack();
void A_SargAttack();
void A_HeadAttack();
void A_BruisAttack();
void A_SkullAttack();
void A_Metal();
void A_SpidRefire();
void A_BabyMetal();
void A_BspiAttack();
void A_Hoof();
void A_CyberAttack();
void A_PainAttack();
void A_PainDie();
void A_KeenDie();
void A_BrainPain();
void A_BrainScream();
void A_BrainDie();
void A_BrainAwake();
void A_BrainSpit();
void A_SpawnSound();
void A_SpawnFly();
void A_BrainExplode();

#define ACTION(name) { #name, A_##name }
static const struct { const char* name; actionf_v action; } deh_actions[] =
{
    ACTION(Light0),
    ACTION(WeaponReady),
    ACTION(Lower),
    ACTION(Raise),
    ACTION(Punch),
    ACTION(ReFire),
    ACTION(FirePistol),
    ACTION(Light1),
    ACTION(FireShotgun),
    ACTION(Light2),
    ACTION(FireShotgun2),
    ACTION(CheckReload),
    ACTION(OpenShotgun2),
    ACTION(LoadShotgun2),
    ACTION(CloseShotgun2),
    ACTION(FireCGun),
    ACTION(GunFlash),
    ACTION(FireMissile),
    ACTION(Saw),
    ACTION(FirePlasma),
    ACTION(BFGsound),
    ACTION(FireBFG),
    ACTION(BFGSpray),
    ACTION(Explode),
    ACTION(Pain),
    ACTION(PlayerScream),
    ACTION(Fall),
    ACTION(XScream),
    ACTION(Look),
    ACTION(Chase),
    ACTION(FaceTarget),
    ACTION(PosAttack),
    ACTION(Scream),
    ACTION(SPosAttack),
    ACTION(VileChase),
    ACTION(VileStart),
    ACTION(VileTarget),
    ACTION(VileAttack),
    ACTION(StartFire),
    ACTION(Fire),
    ACTION(FireCrackle),
    ACTION(Tracer),
    ACTION(SkelWhoosh),
    ACTION(SkelFist),
    ACTION(SkelMissile),
    ACTION(FatRaise),
    ACTION(FatAttack1),
    ACTION(FatAttack2),
    ACTION(FatAttack3),
    ACTION(BossDeath),
    ACTION(CPosAttack),
    ACTION(CPosRefire),
    ACTION(TroopAttack),
    ACTION(SargAttack),
    ACTION(HeadAttack),
    ACTION(BruisAttack),
    ACTION(SkullAttack),
    ACTION(Metal),
    ACTION(SpidRefire),
    ACTION(BabyMetal),
    ACTION(BspiAttack),
    ACTION(Hoof),
    ACTION(CyberAttack),
    ACTION(PainAttack),
    ACTION(PainDie),
    ACTION(KeenDie),
    ACTION(BrainPain),
    ACTION(BrainScream),
    ACTION(BrainDie),
    ACTION(BrainAwake),
    ACTION(BrainSpit),
    ACTION(SpawnSound),
    ACTION(SpawnFly),
    ACTION(BrainExplode),
};

#define BEX(name) { #name, name }
static const struct { const char* name; const char* text; } bex_strings[] =
{
    BEX(PRESSKEY),
    BEX(PRESSYN),
    BEX(QUITMSG),
    BEX(LOADNET),
    BEX(QLOADNET),
    BEX(QSAVESPOT),
    BEX(SAVEDEAD),
    BEX(QSPROMPT),
    BEX(QLPROMPT),
    BEX(NEWGAME),
    BEX(NIGHTMARE),
    BEX(SWSTRING),
    BEX(MSGOFF),
    BEX(MSGON),
    BEX(NETEND),
    BEX(ENDGAME),
    BEX(DOSY),
    BEX(DETAILHI),
    BEX(DETAILLO),
    BEX(GAMMALVL0),
    BEX(GAMMALVL1),
    BEX(GAMMALVL2),
    BEX(GAMMALVL3),
    BEX(GAMMALVL4),
    BEX(EMPTYSTRING),
    BEX(GOTARMOR),
    BEX(GOTMEGA),
    BEX(GOTHTHBONUS),
    BEX(GOTARMBONUS),
    BEX(GOTSTIM),
    BEX(GOTMEDINEED),
    BEX(GOTMEDIKIT),
    BEX(GOTSUPER),
    BEX(GOTBLUECARD),
    BEX(GOTYELWCARD),
    BEX(GOTREDCARD),
    BEX(GOTBLUESKUL),
    BEX(GOTYELWSKUL),
    BEX(GOTREDSKULL),
    BEX(GOTINVUL),
    BEX(GOTBERSERK),
    BEX(GOTINVIS),
    BEX(GOTSUIT),
    BEX(GOTMAP),
    BEX(GOTVISOR),
    BEX(GOTMSPHERE),
    BEX(GOTCLIP),
    BEX(GOTCLIPBOX),
    BEX(GOTROCKET),
    BEX(GOTROCKBOX),
    BEX(GOTCELL),
    BEX(GOTCELLBOX),
    BEX(GOTSHELLS),
    BEX(GOTSHELLBOX),
    BEX(GOTBACKPACK),
    BEX(GOTBFG9000),
    BEX(GOTCHAINGUN),
    BEX(GOTCHAINSAW),
    BEX(GOTLAUNCHER),
    BEX(GOTPLASMA),
    BEX(GOTSHOTGUN),
    BEX(GOTSHOTGUN2),
    BEX(PD_BLUEO),
    BEX(PD_REDO),
    BEX(PD_YELLOWO),
    BEX(PD_BLUEK),
    BEX(PD_REDK),
    BEX(PD_YELLOWK),
    BEX(GGSAVED),
    BEX(HUSTR_MSGU),
    BEX(HUSTR_E1M1),
    BEX(HUSTR_E1M2),
    BEX(HUSTR_E1M3),
    BEX(HUSTR_E1M4),
    BEX(HUSTR_E1M5),
    BEX(HUSTR_E1M6),
    BEX(HUSTR_E1M7),
    BEX(HUSTR_E1M8),
    BEX(HUSTR_E1M9),
    BEX(HUSTR_E2M1),
    BEX(HUSTR_E2M2),
    BEX(HUSTR_E2M3),
    BEX(HUSTR_E2M4),
    BEX(HUSTR_E2M5),
    BEX(HUSTR_E2M6),
    BEX(HUSTR_E2M7),
    BEX(HUSTR_E2M8),
    BEX(HUSTR_E2M9),
    BEX(HUSTR_E3M1),
    BEX(HUSTR_E3M2),
    BEX(HUSTR_E3M3),
    BEX(HUSTR_E3M4),
    BEX(HUSTR_E3M5),
    BEX(HUSTR_E3M6),
    BEX(HUSTR_E3M7),
    BEX(HUSTR_E3M8),
    BEX(HUSTR_E3M9),
    BEX(HUSTR_E4M1),
    BEX(HUSTR_E4M2),
    BEX(HUSTR_E4M3),
    BEX(HUSTR_E4M4),
    BEX(HUSTR_E4M5),
    BEX(HUSTR_E4M6),
    BEX(HUSTR_E4M7),
    BEX(HUSTR_E4M8),
    BEX(HUSTR_E4M9),
    BEX(HUSTR_1),
    BEX(HUSTR_2),
    BEX(HUSTR_3),
    BEX(HUSTR_4),
    BEX(HUSTR_5),
    BEX(HUSTR_6),
    BEX(HUSTR_7),
    BEX(HUSTR_8),
    BEX(HUSTR_9),
    BEX(HUSTR_10),
    BEX(HUSTR_11),
    BEX(HUSTR_12),
    BEX(HUSTR_13),
    BEX(HUSTR_14),
    BEX(HUSTR_15),
    BEX(HUSTR_16),
    BEX(HUSTR_17),
    BEX(HUSTR_18),
    BEX(HUSTR_19),
    BEX(HUSTR_20),
    BEX(HUSTR_21),
    BEX(HUSTR_22),
    BEX(HUSTR_23),
    BEX(HUSTR_24),
    BEX(HUSTR_25),
    BEX(HUSTR_26),
    BEX(HUSTR_27),
    BEX(HUSTR_28),
    BEX(HUSTR_29),
    BEX(HUSTR_30),
    BEX(HUSTR_31),
    BEX(HUSTR_32),
    BEX(PHUSTR_1),
    BEX(PHUSTR_2),
    BEX(PHUSTR_3),
    BEX(PHUSTR_4),
    BEX(PHUSTR_5),
    BEX(PHUSTR_6),
    BEX(PHUSTR_7),
    BEX(PHUSTR_8),
    BEX(PHUSTR_9),
    BEX(PHUSTR_10),
    BEX(PHUSTR_11),
    BEX(PHUSTR_12),
    BEX(PHUSTR_13),
    BEX(PHUSTR_14),
    BEX(PHUSTR_15),
    BEX(PHUSTR_16),
    BEX(PHUSTR_17),
    BEX(PHUSTR_18),
    BEX(PHUSTR_19),
    BEX(PHUSTR_20),
    BEX(PHUSTR_21),
    BEX(PHUSTR_22),
    BEX(PHUSTR_23),
    BEX(PHUSTR_24),
    BEX(PHUSTR_25),
    BEX(PHUSTR_26),
    BEX(PHUSTR_27),
    BEX(PHUSTR_28),
    BEX(PHUSTR_29),
    BEX(PHUSTR_30),
    BEX(PHUSTR_31),
    BEX(PHUSTR_32),
    BEX(THUSTR_1),
    BEX(THUSTR_2),
    BEX(THUSTR_3),
    BEX(THUSTR_4),
    BEX(THUSTR_5),
    BEX(THUSTR_6),
    BEX(THUSTR_7),
    BEX(THUSTR_8),
    BEX(THUSTR_9),
    BEX(THUSTR_10),
    BEX(THUSTR_11),
    BEX(THUSTR_12),
    BEX(THUSTR_13),
    BEX(THUSTR_14),
    BEX(THUSTR_15),
    BEX(THUSTR_16),
    BEX(THUSTR_17),
    BEX(THUSTR_18),
    BEX(THUSTR_19),
    BEX(THUSTR_20),
    BEX(THUSTR_21),
    BEX(THUSTR_22),
    BEX(THUSTR_23),
    BEX(THUSTR_24),
    BEX(THUSTR_25),
    BEX(THUSTR_26),
    BEX(THUSTR_27),
    BEX(THUSTR_28),
    BEX(THUSTR_29),
    BEX(THUSTR_30),
    BEX(THUSTR_31),
    BEX(THUSTR_32),
    BEX(HUSTR_CHATMACRO1),
    BEX(HUSTR_CHATMACRO2),
    BEX(HUSTR_CHATMACRO3),
    BEX(HUSTR_CHATMACRO4),
    BEX(HUSTR_CHATMACRO5),
    BEX(HUSTR_CHATMACRO6),
    BEX(HUSTR_CHATMACRO7),
    BEX(HUSTR_CHATMACRO8),
    BEX(HUSTR_CHATMACRO9),
    BEX(HUSTR_CHATMACRO0),
    BEX(HUSTR_TALKTOSELF1),
    BEX(HUSTR_TALKTOSELF2),
    BEX(HUSTR_TALKTOSELF3),
    BEX(HUSTR_TALKTOSELF4),
    BEX(HUSTR_TALKTOSELF5),
    BEX(HUSTR_MESSAGESENT),
    BEX(HUSTR_PLRGREEN),
    BEX(HUSTR_PLRINDIGO),
    BEX(HUSTR_PLRBROWN),
    BEX(HUSTR_PLRRED),
    BEX(AMSTR_FOLLOWON),
    BEX(AMSTR_FOLLOWOFF),
    BEX(AMSTR_GRIDON),
    BEX(AMSTR_GRIDOFF),
    BEX(AMSTR_MARKEDSPOT),
    BEX(AMSTR_MARKSCLEARED),
    BEX(STSTR_MUS),
    BEX(STSTR_NOMUS),
    BEX(STSTR_DQDON),
    BEX(STSTR_DQDOFF),
    BEX(STSTR_KFAADDED),
    BEX(STSTR_FAADDED),
    BEX(STSTR_NCON),
    BEX(STSTR_NCOFF),
    BEX(STSTR_BEHOLD),
    BEX(STSTR_BEHOLDX),
    BEX(STSTR_CHOPPERS),
    BEX(STSTR_CLEV),
    BEX(E1TEXT),
    BEX(E2TEXT),
    BEX(E3TEXT),
    BEX(E4TEXT),
    BEX(C1TEXT),
    BEX(C2TEXT),
    BEX(C3TEXT),
    BEX(C4TEXT),
    BEX(C5TEXT),
    BEX(C6TEXT),
    BEX(P1TEXT),
    BEX(P2TEXT),
    BEX(P3TEXT),
    BEX(P4TEXT),
    BEX(P5TEXT),
    BEX(P6TEXT),
    BEX(T1TEXT),
    BEX(T2TEXT),
    BEX(T3TEXT),
    BEX(T4TEXT),
    BEX(T5TEXT),
    BEX(T6TEXT),
    BEX(CC_ZOMBIE),
    BEX(CC_SHOTGUN),
    BEX(CC_HEAVY),
    BEX(CC_IMP),
    BEX(CC_DEMON),
    BEX(CC_LOST),
    BEX(CC_CACO),
    BEX(CC_HELL),
    BEX(CC_BARON),
    BEX(CC_ARACH),
    BEX(CC_PAIN),
    BEX(CC_REVEN),
    BEX(CC_MANCU),
    BEX(CC_ARCH),
    BEX(CC_SPIDER),
    BEX(CC_CYBER),
    BEX(CC_HERO),
    // Finale backgrounds.
    { "BGFLATE1", "FLOOR4_8" },
    { "BGFLATE2", "SFLR6_1" },
    { "BGFLATE3", "MFLR8_4" },
    { "BGFLATE4", "MFLR8_3" },
    { "BGFLAT06", "SLIME16" },
    { "BGFLAT11", "RROCK14" },
    { "BGFLAT20", "RROCK07" },
    { "BGFLAT30", "RROCK17" },
    { "BGFLAT15", "RROCK13" },
    { "BGFLAT31", "RROCK19" },
    { "BGCASTCALL", "BOSSBACK" },
};

static const struct { const char* name; int bits; } deh_flags[] =
{
    { "SPECIAL", MF_SPECIAL },		{ "SOLID", MF_SOLID },
    { "SHOOTABLE", MF_SHOOTABLE },	{ "NOSECTOR", MF_NOSECTOR },
    { "NOBLOCKMAP", MF_NOBLOCKMAP },	{ "AMBUSH", MF_AMBUSH },
    { "JUSTHIT", MF_JUSTHIT },		{ "JUSTATTACKED", MF_JUSTATTACKED },
    { "SPAWNCEILING", MF_SPAWNCEILING }, { "NOGRAVITY", MF_NOGRAVITY },
    { "DROPOFF", MF_DROPOFF },		{ "PICKUP", MF_PICKUP },
    { "NOCLIP", MF_NOCLIP },		{ "SLIDE", MF_SLIDE },
    { "FLOAT", MF_FLOAT },		{ "TELEPORT", MF_TELEPORT },
    { "MISSILE", MF_MISSILE },		{ "DROPPED", MF_DROPPED },
    { "SHADOW", MF_SHADOW },		{ "NOBLOOD", MF_NOBLOOD },
    { "CORPSE", MF_CORPSE },		{ "INFLOAT", MF_INFLOAT },
    { "COUNTKILL", MF_COUNTKILL },	{ "COUNTITEM", MF_COUNTITEM },
    { "SKULLFLY", MF_SKULLFLY },	{ "NOTDMATCH", MF_NOTDMATCH },
    { "TRANSLATION", 1 << MF_TRANSSHIFT }, { "TRANSLATION1", 1 << MF_TRANSSHIFT },
    { "TRANSLATION2", 2 << MF_TRANSSHIFT }, { "UNUSED1", 2 << MF_TRANSSHIFT },
};

typedef enum { FIELD_INT, FIELD_STATE, FIELD_SOUND, FIELD_FLAGS } fieldkind_t;

#define THING(key, field, kind) { key, offsetof(mobjinfo_t, field), kind }
static const struct { const char* key; size_t offset; fieldkind_t kind; } thing_fields[] =
{
    THING("ID #", doomednum, FIELD_INT),
    THING("Initial frame", spawnstate, FIELD_STATE),
    THING("Hit points", spawnhealth, FIELD_INT),
    THING("First moving frame", seestate, FIELD_STATE),
    THING("Alert sound", seesound, FIELD_SOUND),
    THING("Reaction time", reactiontime, FIELD_INT),
    THING("Attack sound", attacksound, FIELD_SOUND),
    THING("Injury frame", painstate, FIELD_STATE),
    THING("Pain chance", painchance, FIELD_INT),
    THING("Pain sound", painsound, FIELD_SOUND),
    THING("Close attack frame", meleestate, FIELD_STATE),
    THING("Far attack frame", missilestate, FIELD_STATE),
    THING("Death frame", deathstate, FIELD_STATE),
    THING("Exploding frame", xdeathstate, FIELD_STATE),
    THING("Death sound", deathsound, FIELD_SOUND),
    THING("Speed", speed, FIELD_INT),
    THING("Width", radius, FIELD_INT),
    THING("Height", height, FIELD_INT),
    THING("Mass", mass, FIELD_INT),
    THING("Missile damage", damage, FIELD_INT),
    THING("Action sound", activesound, FIELD_SOUND),
    THING("Bits", flags, FIELD_FLAGS),
    THING("Respawn frame", raisestate, FIELD_STATE),
};

#define WEAPON(key, field) { key, offsetof(weaponinfo_t, field) }
static const struct { const char* key; size_t offset; } weapon_fields[] =
{
    WEAPON("Deselect frame", upstate),
    WEAPON("Select frame", downstate),
    WEAPON("Bobbing frame", readystate),
    WEAPON("Shooting frame", atkstate),
    WEAPON("Firing frame", flashstate),
};

static const struct { const char* key; int* value; } misc_fields[] =
{
    { "Initial Health", &deh_initial_health },
    { "Initial Bullets", &deh_initial_bullets },
    { "Max Health", &deh_max_health },
    { "Max Armor", &deh_max_armor },
    { "Green Armor Class", &deh_green_armor_class },
    { "Blue Armor Class", &deh_blue_armor_class },
    { "Max Soulsphere", &deh_max_soulsphere },
    { "Soulsphere Health", &deh_soulsphere_health },
    { "Megasphere Health", &deh_megasphere_health },
    { "God Mode Health", &deh_god_mode_health },
    { "IDFA Armor", &deh_idfa_armor },
    { "IDFA Armor Class", &deh_idfa_armor_class },
    { "IDKFA Armor", &deh_idkfa_armor },
    { "IDKFA Armor Class", &deh_idkfa_armor_class },
    { "BFG Cells/Shot", &deh_bfg_cells_per_shot },
};

// Cheats with parameters (idmus, idclev) take two digits after the text.
static const struct { const char* key; cheatseq_t* cheat; int params; } cheat_fields[] =
{
    { "Change music", &cheat_mus, 2 },
    { "Chainsaw", &cheat_choppers, 0 },
    { "God mode", &cheat_god, 0 },
    { "Ammo & Keys", &cheat_ammo, 0 },
    { "Ammo", &cheat_ammonokey, 0 },
    { "No Clipping 1", &cheat_noclip, 0 },
    { "No Clipping 2", &cheat_commercial_noclip, 0 },
    { "Invincibility", &cheat_powerup[0], 0 },
    { "Berserk", &cheat_powerup[1], 0 },
    { "Invisibility", &cheat_powerup[2], 0 },
    { "Radiation Suit", &cheat_powerup[3], 0 },
    { "Auto-map", &cheat_powerup[4], 0 },
    { "Lite-Amp Goggles", &cheat_powerup[5], 0 },
    { "BEHOLD menu", &cheat_powerup[6], 0 },
    { "Level Warp", &cheat_clev, 2 },
    { "Player Position", &cheat_mypos, 0 },
    { "Map cheat", &cheat_amap, 0 },
};

typedef enum
{
    BLOCK_NONE, BLOCK_SKIP, BLOCK_THING, BLOCK_FRAME, BLOCK_POINTER,
    BLOCK_SOUND, BLOCK_AMMO, BLOCK_WEAPON, BLOCK_CHEAT, BLOCK_MISC,
    BLOCK_STRINGS, BLOCK_PARS, BLOCK_CODEPTR, BLOCK_SPRITES,
    BLOCK_SOUNDS, BLOCK_MUSIC
} block_t;

typedef struct
{
    const char*	source;
    char*	cursor;		// unread text
    int		line;		// number of the line last read
    int		warnings;
    block_t	block;
    int		index;
} patch_t;

typedef struct { char* from; char* to; } replacement_t;

static replacement_t*	replacements;
static int		numreplacements;
static int		maxreplacements;
static actionf_t	original_actions[NUMSTATES];

#define MAXWARNINGS 20

static void DEH_Warn (patch_t* patch, const char* format, ...)
{
    va_list	args;

    if (++patch->warnings > MAXWARNINGS)
	return;
    fprintf (stderr, "DEHACKED %s:%d: ", patch->source, patch->line);
    va_start (args, format);
    vfprintf (stderr, format, args);
    va_end (args);
    fputc ('\n', stderr);
    if (patch->warnings == MAXWARNINGS)
	fprintf (stderr, "DEHACKED %s: further warnings suppressed\n", patch->source);
}

static char* DEH_Copy (const char* text)
{
    char* copy = malloc (strlen (text) + 1);

    if (!copy)
	I_Error ("DEHACKED: out of memory");
    return strcpy (copy, text);
}

static boolean DEH_Same (const char* a, const char* b)
{
    while (*a && tolower ((unsigned char)*a) == tolower ((unsigned char)*b))
	a++, b++;
    return !*a && !*b;
}

// Case-insensitive prefix test; returns the text after the prefix.
static char* DEH_Prefix (char* text, const char* prefix)
{
    while (*prefix && tolower ((unsigned char)*text) == tolower ((unsigned char)*prefix))
	text++, prefix++;
    return *prefix ? NULL : text;
}

static char* DEH_Trim (char* text)
{
    char* end;

    while (isspace ((unsigned char)*text))
	text++;
    end = text + strlen (text);
    while (end > text && isspace ((unsigned char)end[-1]))
	*--end = 0;
    return text;
}

// Parses a whole decimal or 0x-prefixed hexadecimal number.
static boolean DEH_Number (const char* text, int* value)
{
    char*	end;
    long	number;

    while (isspace ((unsigned char)*text))
	text++;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
	number = strtol (text + 2, &end, 16);
    else
	number = strtol (text, &end, 10);
    if (end == text)
	return false;
    while (isspace ((unsigned char)*end))
	end++;
    if (*end)
	return false;
    *value = (int)number;
    return true;
}

static char* DEH_ReadLine (patch_t* patch)
{
    char* line = patch->cursor;
    char* end;

    if (!*line)
	return NULL;
    end = strchr (line, '\n');
    if (end)
    {
	*end = 0;
	patch->cursor = end + 1;
    }
    else
	patch->cursor = line + strlen (line);
    patch->line++;
    return line;
}

static void DEH_Replace (const char* from, const char* to)
{
    int i;

    for (i = 0; i < numreplacements; i++)
	if (!strcmp (replacements[i].from, from))
	{
	    free (replacements[i].to);
	    replacements[i].to = DEH_Copy (to);
	    return;
	}
    if (numreplacements == maxreplacements)
    {
	maxreplacements = maxreplacements ? maxreplacements * 2 : 64;
	replacements = realloc (replacements, maxreplacements * sizeof(*replacements));
	if (!replacements)
	    I_Error ("DEHACKED: out of memory");
    }
    replacements[numreplacements].from = DEH_Copy (from);
    replacements[numreplacements].to = DEH_Copy (to);
    numreplacements++;
}

char* DEH_String (const char* text)
{
    int i;

    for (i = 0; i < numreplacements; i++)
	if (!strcmp (replacements[i].from, text))
	    return replacements[i].to;
    return (char*)text;
}

//
// Renames. Sprite, sound and music names live in tables rather than
// in the replaceable strings.
//
static boolean DEH_RenameSprite (const char* from, const char* to)
{
    int i;

    if (strlen (to) != 4)
	return false;
    for (i = 0; i < NUMSPRITES; i++)
	if (DEH_Same (sprnames[i], from))
	{
	    char* name = DEH_Copy (to);
	    char* c;
	    for (c = name; *c; c++)
		*c = toupper ((unsigned char)*c);
	    sprnames[i] = name;
	    return true;
	}
    return false;
}

static boolean DEH_RenameSound (const char* from, const char* to)
{
    int i;

    for (i = 1; i < NUMSFX; i++)
	if (S_sfx[i].name && DEH_Same (S_sfx[i].name, from))
	{
	    S_sfx[i].name = DEH_Copy (to);
	    return true;
	}
    return false;
}

static boolean DEH_RenameMusic (const char* from, const char* to)
{
    int i;

    for (i = 1; i < NUMMUSIC; i++)
	if (S_music[i].name && DEH_Same (S_music[i].name, from))
	{
	    S_music[i].name = DEH_Copy (to);
	    return true;
	}
    return false;
}

//
// Text: replaces the next fromlen characters with the following tolen.
//
static void DEH_Text (patch_t* patch, int fromlen, int tolen)
{
    char*	from;
    char*	to;
    char*	start = patch->cursor;
    int		i;

    if (fromlen < 0 || tolen < 0 || (size_t)fromlen + tolen > strlen (start))
    {
	DEH_Warn (patch, "Text block runs past the end of the patch");
	patch->cursor = start + strlen (start);
	return;
    }
    from = malloc (fromlen + 1);
    to = malloc (tolen + 1);
    if (!from || !to)
	I_Error ("DEHACKED: out of memory");
    memcpy (from, start, fromlen);
    from[fromlen] = 0;
    memcpy (to, start + fromlen, tolen);
    to[tolen] = 0;
    for (i = 0; i < fromlen + tolen; i++)
	if (start[i] == '\n')
	    patch->line++;
    patch->cursor = start + fromlen + tolen;

    if (!DEH_RenameSprite (from, to) && !DEH_RenameSound (from, to))
	DEH_RenameMusic (from, to);
    DEH_Replace (from, to);
    free (from);
    free (to);
}

static void DEH_Header (patch_t* patch, char* line)
{
    static const struct { const char* name; block_t block; int count; int base; } headers[] =
    {
	{ "Thing", BLOCK_THING, NUMMOBJTYPES, 1 },
	{ "Frame", BLOCK_FRAME, NUMSTATES, 0 },
	{ "Sound", BLOCK_SOUND, NUMSFX, 0 },
	{ "Ammo", BLOCK_AMMO, NUMAMMO, 0 },
	{ "Weapon", BLOCK_WEAPON, NUMWEAPONS, 0 },
	{ "Cheat", BLOCK_CHEAT, 1, 0 },
	{ "Misc", BLOCK_MISC, 1, 0 },
    };
    static const struct { const char* name; block_t block; } sections[] =
    {
	{ "[STRINGS]", BLOCK_STRINGS }, { "[PARS]", BLOCK_PARS },
	{ "[CODEPTR]", BLOCK_CODEPTR }, { "[SPRITES]", BLOCK_SPRITES },
	{ "[SOUNDS]", BLOCK_SOUNDS }, { "[MUSIC]", BLOCK_MUSIC },
    };
    char*	rest;
    int		i;
    long	number;

    patch->block = BLOCK_SKIP;
    if (line[0] == '[')
    {
	for (i = 0; i < (int)(sizeof(sections) / sizeof(*sections)); i++)
	    if (DEH_Same (line, sections[i].name))
	    {
		patch->block = sections[i].block;
		return;
	    }
	DEH_Warn (patch, "unsupported section %s", line);
	return;
    }
    if ((rest = DEH_Prefix (line, "Patch File")) != NULL)
    {
	patch->block = BLOCK_NONE;
	return;
    }
    if ((rest = DEH_Prefix (line, "Text")) != NULL && isspace ((unsigned char)*rest))
    {
	char*	end;
	long	fromlen = strtol (rest, &end, 10);
	long	tolen = strtol (end, NULL, 10);

	DEH_Text (patch, (int)fromlen, (int)tolen);
	return;
    }
    if ((rest = DEH_Prefix (line, "Pointer")) != NULL && isspace ((unsigned char)*rest))
    {
	// "Pointer 12 (Frame 34)": the frame number is what matters.
	char* frame = strchr (rest, '(');
	if (frame && (frame = DEH_Prefix (DEH_Trim (frame + 1), "Frame")) != NULL)
	{
	    number = strtol (frame, NULL, 10);
	    if (number >= 0 && number < NUMSTATES)
	    {
		patch->block = BLOCK_POINTER;
		patch->index = (int)number;
		return;
	    }
	}
	DEH_Warn (patch, "bad pointer block \"%s\"", line);
	return;
    }
    for (i = 0; i < (int)(sizeof(headers) / sizeof(*headers)); i++)
    {
	if ((rest = DEH_Prefix (line, headers[i].name)) == NULL || !isspace ((unsigned char)*rest))
	    continue;
	number = strtol (rest, NULL, 10) - headers[i].base;
	if (number < 0 || number >= headers[i].count)
	{
	    DEH_Warn (patch, "%s is out of range for this engine", line);
	    return;
	}
	patch->block = headers[i].block;
	patch->index = (int)number;
	return;
    }
    DEH_Warn (patch, "unsupported block \"%s\"", line);
}

static boolean DEH_CheckRange (patch_t* patch, const char* key, int value, int count)
{
    if (value >= 0 && value < count)
	return true;
    DEH_Warn (patch, "%s = %d is out of range for this engine", key, value);
    return false;
}

static boolean DEH_Flags (patch_t* patch, const char* text, int* bits)
{
    char	buffer[256];
    char*	name;
    int		i;

    if (DEH_Number (text, bits))
	return true;
    *bits = 0;
    snprintf (buffer, sizeof(buffer), "%s", text);
    for (name = strtok (buffer, "+|, \t"); name; name = strtok (NULL, "+|, \t"))
    {
	for (i = 0; i < (int)(sizeof(deh_flags) / sizeof(*deh_flags)); i++)
	    if (DEH_Same (name, deh_flags[i].name))
		break;
	if (i == (int)(sizeof(deh_flags) / sizeof(*deh_flags)))
	{
	    DEH_Warn (patch, "unsupported thing flag %s", name);
	    continue;
	}
	*bits |= deh_flags[i].bits;
    }
    return true;
}

static void DEH_Thing (patch_t* patch, const char* key, const char* value)
{
    mobjinfo_t*	info = &mobjinfo[patch->index];
    int		number;
    int		i;

    for (i = 0; i < (int)(sizeof(thing_fields) / sizeof(*thing_fields)); i++)
	if (DEH_Same (key, thing_fields[i].key))
	    break;
    if (i == (int)(sizeof(thing_fields) / sizeof(*thing_fields)))
    {
	DEH_Warn (patch, "unsupported thing field \"%s\"", key);
	return;
    }
    if (thing_fields[i].kind == FIELD_FLAGS)
	DEH_Flags (patch, value, &number);
    else if (!DEH_Number (value, &number))
    {
	DEH_Warn (patch, "%s needs a number", key);
	return;
    }
    if (thing_fields[i].kind == FIELD_STATE && !DEH_CheckRange (patch, key, number, NUMSTATES))
	return;
    if (thing_fields[i].kind == FIELD_SOUND && !DEH_CheckRange (patch, key, number, NUMSFX))
	return;
    *(int*)((byte*)info + thing_fields[i].offset) = number;
}

static void DEH_Frame (patch_t* patch, const char* key, int value)
{
    state_t* state = &states[patch->index];

    if (DEH_Same (key, "Sprite number"))
    {
	if (DEH_CheckRange (patch, key, value, NUMSPRITES))
	    state->sprite = value;
    }
    else if (DEH_Same (key, "Sprite subnumber"))
	state->frame = value;
    else if (DEH_Same (key, "Duration"))
	state->tics = value;
    else if (DEH_Same (key, "Next frame"))
    {
	if (DEH_CheckRange (patch, key, value, NUMSTATES))
	    state->nextstate = value;
    }
    else if (DEH_Same (key, "Unknown 1"))
	state->misc1 = value;
    else if (DEH_Same (key, "Unknown 2"))
	state->misc2 = value;
    else
	DEH_Warn (patch, "unsupported frame field \"%s\"", key);
}

static void DEH_Weapon (patch_t* patch, const char* key, int value)
{
    weaponinfo_t*	weapon = &weaponinfo[patch->index];
    int			i;

    if (DEH_Same (key, "Ammo type"))
    {
	// am_noammo is NUMAMMO + 1 in vanilla's enum.
	if (value == am_noammo || DEH_CheckRange (patch, key, value, NUMAMMO))
	    weapon->ammo = value;
	return;
    }
    for (i = 0; i < (int)(sizeof(weapon_fields) / sizeof(*weapon_fields)); i++)
	if (DEH_Same (key, weapon_fields[i].key))
	{
	    if (DEH_CheckRange (patch, key, value, NUMSTATES))
		*(int*)((byte*)weapon + weapon_fields[i].offset) = value;
	    return;
	}
    DEH_Warn (patch, "unsupported weapon field \"%s\"", key);
}

static void DEH_Misc (patch_t* patch, const char* key, int value)
{
    int i;

    if (DEH_Same (key, "Monsters Infight"))
    {
	// DeHackEd writes 202 for on and 221 for off.
	deh_species_infighting = value == 202;
	return;
    }
    for (i = 0; i < (int)(sizeof(misc_fields) / sizeof(*misc_fields)); i++)
	if (DEH_Same (key, misc_fields[i].key))
	{
	    *misc_fields[i].value = value;
	    return;
	}
    DEH_Warn (patch, "unsupported misc field \"%s\"", key);
}

static void DEH_Cheat (patch_t* patch, const char* key, const char* value)
{
    unsigned char*	sequence;
    int			i;
    int			length = strlen (value);

    for (i = 0; i < (int)(sizeof(cheat_fields) / sizeof(*cheat_fields)); i++)
	if (DEH_Same (key, cheat_fields[i].key))
	    break;
    if (i == (int)(sizeof(cheat_fields) / sizeof(*cheat_fields)))
    {
	DEH_Warn (patch, "unsupported cheat \"%s\"", key);
	return;
    }
    if (!length)
	return;
    // Scrambled text, optional parameter slots, then the 0xff end marker.
    sequence = malloc (length + (cheat_fields[i].params ? cheat_fields[i].params + 1 : 0) + 1);
    if (!sequence)
	I_Error ("DEHACKED: out of memory");
    for (int c = 0; c < length; c++)
	sequence[c] = SCRAMBLE (tolower ((unsigned char)value[c]));
    if (cheat_fields[i].params)
    {
	sequence[length++] = 1;
	for (int c = 0; c < cheat_fields[i].params; c++)
	    sequence[length++] = 0;
    }
    sequence[length] = 0xff;
    cheat_fields[i].cheat->sequence = sequence;
    cheat_fields[i].cheat->p = NULL;
}

static const char* DEH_BexOriginal (const char* name)
{
    char*	rest;
    int		i;

    // QUITMSG1-7 are DOOM's quit messages, QUITMSG8-14 DOOM II's.
    if ((rest = DEH_Prefix ((char*)name, "QUITMSG")) != NULL && *rest)
    {
	int number;
	if (!DEH_Number (rest, &number) || number < 1 || number > 14)
	    return NULL;
	return number < NUM_QUITMESSAGES ? doom1_endmsg[number]
	    : doom2_endmsg[number - NUM_QUITMESSAGES + 1];
    }
    for (i = 0; i < (int)(sizeof(bex_strings) / sizeof(*bex_strings)); i++)
	if (DEH_Same (name, bex_strings[i].name))
	    return bex_strings[i].text;
    return NULL;
}

// Reads a [STRINGS] value: a trailing backslash continues it on the next
// line, and \n is a newline.
static char* DEH_BexValue (patch_t* patch, char* value)
{
    size_t	capacity = strlen (value) + 64;
    size_t	length = 0;
    char*	text = malloc (capacity);
    char*	part = value;

    if (!text)
	I_Error ("DEHACKED: out of memory");
    for (;;)
    {
	size_t	partlength = strlen (part);
	boolean	more = partlength && part[partlength - 1] == '\\';

	if (more)
	    part[--partlength] = 0;
	if (length + partlength + 1 > capacity)
	{
	    capacity = (length + partlength + 1) * 2;
	    text = realloc (text, capacity);
	    if (!text)
		I_Error ("DEHACKED: out of memory");
	}
	for (char* c = part; *c; c++)
	{
	    if (c[0] == '\\' && (c[1] == 'n' || c[1] == 'N'))
	    {
		text[length++] = '\n';
		c++;
	    }
	    else
		text[length++] = *c;
	}
	if (!more || !(part = DEH_ReadLine (patch)))
	    break;
	part = DEH_Trim (part);
    }
    text[length] = 0;
    return text;
}

static void DEH_Pars (patch_t* patch, char* line)
{
    int		numbers[3];
    int		count = 0;
    char*	text = DEH_Prefix (line, "par");
    char*	end;

    while (text && count < 3)
    {
	long number = strtol (text, &end, 10);
	if (end == text)
	    break;
	numbers[count++] = (int)number;
	text = end;
    }
    if (count == 3)
    {
	if (numbers[0] >= 1 && numbers[0] <= 3 && numbers[1] >= 1 && numbers[1] <= 9)
	    pars[numbers[0]][numbers[1]] = numbers[2];
    }
    else if (count == 2)
    {
	if (numbers[0] >= 1 && numbers[0] <= 32)
	    cpars[numbers[0] - 1] = numbers[1];
    }
    else
	DEH_Warn (patch, "bad par line \"%s\"", line);
}

static void DEH_CodePointer (patch_t* patch, const char* key, const char* value)
{
    char*	frame = DEH_Prefix ((char*)key, "FRAME");
    const char*	name = value;
    int		number;
    int		i;

    if (!frame || !DEH_Number (frame, &number) || !DEH_CheckRange (patch, key, number, NUMSTATES))
	return;
    if (DEH_Same (name, "NULL"))
    {
	states[number].action.acv = NULL;
	return;
    }
    if (DEH_Prefix ((char*)name, "A_"))
	name += 2;
    for (i = 0; i < (int)(sizeof(deh_actions) / sizeof(*deh_actions)); i++)
	if (DEH_Same (name, deh_actions[i].name))
	{
	    states[number].action.acv = deh_actions[i].action;
	    return;
	}
    DEH_Warn (patch, "unsupported code pointer %s", value);
}

// [SPRITES], [SOUNDS] and [MUSIC]: "old = new", by name or number.
static void DEH_Rename (patch_t* patch, const char* key, const char* value)
{
    int		number;
    boolean	done = false;

    if (DEH_Number (key, &number))
    {
	if (patch->block == BLOCK_SPRITES && number >= 0 && number < NUMSPRITES)
	    key = sprnames[number];
	else if (patch->block == BLOCK_SOUNDS && number > 0 && number < NUMSFX)
	    key = S_sfx[number].name;
	else if (patch->block == BLOCK_MUSIC && number > 0 && number < NUMMUSIC)
	    key = S_music[number].name;
	else
	{
	    DEH_Warn (patch, "%s is out of range for this engine", key);
	    return;
	}
    }
    if (patch->block == BLOCK_SPRITES)
	done = DEH_RenameSprite (key, value);
    else if (patch->block == BLOCK_SOUNDS)
	done = DEH_RenameSound (key, value);
    else
	done = DEH_RenameMusic (key, value);
    if (!done)
	DEH_Warn (patch, "cannot rename %s", key);
}

static void DEH_Value (patch_t* patch, char* key, char* value)
{
    int number;

    switch (patch->block)
    {
      case BLOCK_STRINGS:
	{
	    const char* original = DEH_BexOriginal (key);
	    char* text = DEH_BexValue (patch, value);
	    // Names other ports added (obituaries, menus) have nothing to replace.
	    if (original)
		DEH_Replace (original, text);
	    free (text);
	}
	return;
      case BLOCK_CODEPTR:
	DEH_CodePointer (patch, key, value);
	return;
      case BLOCK_SPRITES:
      case BLOCK_SOUNDS:
      case BLOCK_MUSIC:
	DEH_Rename (patch, key, value);
	return;
      case BLOCK_THING:
	DEH_Thing (patch, key, value);
	return;
      case BLOCK_CHEAT:
	DEH_Cheat (patch, key, value);
	return;
      case BLOCK_NONE:
      case BLOCK_SKIP:
      case BLOCK_PARS:
	return;
      default:
	break;
    }
    if (!DEH_Number (value, &number))
    {
	DEH_Warn (patch, "%s needs a number", key);
	return;
    }
    switch (patch->block)
    {
      case BLOCK_FRAME:
	DEH_Frame (patch, key, number);
	break;
      case BLOCK_POINTER:
	if (!DEH_Same (key, "Codep Frame"))
	    DEH_Warn (patch, "unsupported pointer field \"%s\"", key);
	else if (DEH_CheckRange (patch, key, number, NUMSTATES))
	    states[patch->index].action = original_actions[number];
	break;
      case BLOCK_SOUND:
	if (DEH_Same (key, "Zero/One"))
	    S_sfx[patch->index].singularity = number;
	else if (DEH_Same (key, "Value"))
	    S_sfx[patch->index].priority = number;
	break;
      case BLOCK_AMMO:
	if (DEH_Same (key, "Max ammo"))
	    maxammo[patch->index] = number;
	else if (DEH_Same (key, "Per ammo"))
	    clipammo[patch->index] = number;
	else
	    DEH_Warn (patch, "unsupported ammo field \"%s\"", key);
	break;
      case BLOCK_WEAPON:
	DEH_Weapon (patch, key, number);
	break;
      case BLOCK_MISC:
	DEH_Misc (patch, key, number);
	break;
      default:
	break;
    }
}

static void DEH_Parse (const char* source, char* text)
{
    patch_t	patch = { source, text, 0, 0, BLOCK_NONE, 0 };
    char*	line;
    char*	from;
    char*	to;

    // Carriage returns never count, not even inside Text blocks.
    for (from = to = text; *from; from++)
	if (*from != '\r')
	    *to++ = *from;
    *to = 0;

    while ((line = DEH_ReadLine (&patch)) != NULL)
    {
	char* equals;

	line = DEH_Trim (line);
	if (!*line || *line == '#')
	    continue;
	if (patch.block == BLOCK_PARS && DEH_Prefix (line, "par")
	    && isspace ((unsigned char)line[3]))
	{
	    DEH_Pars (&patch, line);
	    continue;
	}
	equals = strchr (line, '=');
	if (!equals || line[0] == '[')
	{
	    DEH_Header (&patch, line);
	    continue;
	}
	*equals = 0;
	DEH_Value (&patch, DEH_Trim (line), DEH_Trim (equals + 1));
    }
}

static void DEH_LoadLump (int lump)
{
    int		length = W_LumpLength (lump);
    char*	text = malloc (length + 1);
    char	source[64];

    if (!text)
	I_Error ("DEHACKED: out of memory");
    W_ReadLump (lump, text);
    text[length] = 0;
    snprintf (source, sizeof(source), "lump %d", lump);
    DEH_Parse (source, text);
    free (text);
}

static void DEH_LoadFile (const char* path)
{
    FILE*	file = fopen (path, "rb");
    long	length;
    char*	text;

    if (!file || fseek (file, 0, SEEK_END) || (length = ftell (file)) < 0
	|| fseek (file, 0, SEEK_SET))
    {
	if (file)
	    fclose (file);
	I_Error ("Could not read DEHACKED patch %s", path);
    }
    text = malloc (length + 1);
    if (!text)
	I_Error ("DEHACKED: out of memory");
    length = (long)fread (text, 1, length, file);
    fclose (file);
    text[length] = 0;
    printf ("DEH_Init: applying %s\n", path);
    DEH_Parse (path, text);
    free (text);
}

void DEH_Init (void)
{
    int i;
    int p;

    for (i = 0; i < NUMSTATES; i++)
	original_actions[i] = states[i].action;

    // -nodeh skips the patches embedded in the WADs.
    if (!M_CheckParm ("-nodeh"))
	for (i = 0; i < numlumps; i++)
	    if (!strncmp (lumpinfo[i].name, "DEHACKED", 8))
	    {
		printf ("DEH_Init: applying DEHACKED lump %d\n", i);
		DEH_LoadLump (i);
	    }

    for (p = 1; p < myargc; p++)
	if (DEH_Same (myargv[p], "-deh") || DEH_Same (myargv[p], "-bex"))
	    while (p + 1 < myargc && myargv[p + 1][0] != '-')
		DEH_LoadFile (myargv[++p]);
}
