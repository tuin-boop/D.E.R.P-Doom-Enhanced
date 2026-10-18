/* DOOM's music calls, played on one of three devices:
 * - SoundFont: a .sf2 bank rendered in the mixer. A bank the player puts in
 *   soundfonts/ beside doom.cfg wins over the bundled GeneralUser GS.
 * - OPL: FM synthesis from the WAD's GENMIDI instruments, as on DOS.
 * - System (Mac only): the Mac's own General MIDI synthesizer.
 * F4 picks one; -soundfont [bank.sf2], -opl or -systemsynth override it
 * for the session. Without a bank, SoundFont falls back to the system
 * synthesizer on the Mac and to the OPL elsewhere. */
#include <stdio.h>
#include <string.h>
#include "doomdef.h"
#include "i_sound.h"
#include "m_argv.h"
#include "i_music.h"
#include "audio/sound_options.h"
#include <SDL3/SDL.h>

#ifdef __APPLE__
#define HAVE_SYSTEM_SYNTH 1
#else
#define HAVE_SYSTEM_SYNTH 0
#endif
#define BUNDLED_BANK "soundfonts/GeneralUser-GS.sf2"

static int setting = MUSIC_SOUNDFONT;	/* the F4 choice */
static int device = -1;			/* where the current song plays */
static int bank_state;			/* 0 not looked for, 1 in use, -1 none */
static void *song;
static int on_device, playing, looping, volume = 8;

static int forced_device(void)
{
    if (M_CheckParm("-opl")) return MUSIC_OPL;
    if (M_CheckParm("-soundfont")) return MUSIC_SOUNDFONT;
    if (M_CheckParm("-systemsynth")) return MUSIC_SYSTEM;
    return -1;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file) fclose(file);
    return file != NULL;
}

/* -soundfont <bank>, then the first bank in soundfonts/, then the bundled one. */
static int find_bank(char *path, size_t size)
{
    int arg = M_CheckParm("-soundfont");
    if (arg && arg + 1 < myargc && myargv[arg + 1][0] != '-')
    {
        snprintf(path, size, "%s", myargv[arg + 1]);
        return 1;
    }
    int count = 0;
    char **found = SDL_GlobDirectory("soundfonts", "*.sf2", SDL_GLOB_CASEINSENSITIVE, &count);
    const char *first = NULL;
    for (int i = 0; found && i < count; ++i)
        if (!first || SDL_strcasecmp(found[i], first) < 0) first = found[i];
    if (first) snprintf(path, size, "soundfonts/%s", first);
    SDL_free(found);
    if (first) return 1;
    const char *base = SDL_GetBasePath();
    snprintf(path, size, "%s" BUNDLED_BANK, base ? base : "./");
    return file_exists(path);
}

static int bank_ready(void)
{
    if (!bank_state)
    {
        char path[4096];
        bank_state = -1;
        if (find_bank(path, sizeof(path)) && I_Mixer_LoadSoundFont(path))
        {
            bank_state = 1;
            printf("SoundFont music: %s\n", path);
        }
        else fprintf(stderr, "No SoundFont bank to play music with.\n");
    }
    return bank_state > 0;
}

/* The device a choice really plays on. */
static int resolve(int choice)
{
    int forced = forced_device();
    if (forced >= 0) choice = forced;
    if (choice == MUSIC_SYSTEM && !HAVE_SYSTEM_SYNTH) choice = MUSIC_SOUNDFONT;
    if (choice == MUSIC_SOUNDFONT && !bank_ready()) choice = HAVE_SYSTEM_SYNTH ? MUSIC_SYSTEM : MUSIC_OPL;
    return choice;
}

static void set_volume(void)
{
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) { Mac_SetMusicVolume(volume); return; }
#endif
    I_Mixer_SetMusicVolume(volume);
}

static void start(void)
{
    device = resolve(setting);
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) on_device = Mac_RegisterSong(song);
    else
#endif
    on_device = I_Mixer_RegisterSong(song, device == MUSIC_SOUNDFONT);
    set_volume();
}

static void stop(void)
{
    if (!on_device) return;
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) Mac_UnRegisterSong();
    else
#endif
    I_Mixer_UnRegisterSong();
    on_device = 0;
}

static void play(void)
{
    if (!on_device) return;
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) { Mac_PlaySong(looping); return; }
#endif
    I_Mixer_PlaySong(looping);
}

void I_InitMusic(void) { }
void I_ShutdownMusic(void) { stop(); song = NULL; playing = 0; }

void I_SetMusicVolume(int level)
{
    volume = level;
    if (on_device) set_volume();
}

/* The handle only tells DOOM a song is there: a device switch registers
   the song again without DOOM knowing. */
int I_RegisterSong(void *data)
{
    stop();
    song = data;
    playing = 0;
    if (song) start();
    return song != NULL;
}

void I_UnRegisterSong(int handle) { (void)handle; I_ShutdownMusic(); }

void I_PlaySong(int handle, int loop)
{
    (void)handle;
    playing = 1;
    looping = loop;
    play();
}

void I_StopSong(int handle)
{
    (void)handle;
    playing = 0;
    if (!on_device) return;
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) { Mac_StopSong(); return; }
#endif
    I_Mixer_StopSong();
}

void I_PauseSong(int handle)
{
    (void)handle;
    if (!on_device) return;
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) { Mac_PauseSong(); return; }
#endif
    I_Mixer_PauseSong();
}

void I_ResumeSong(int handle)
{
    (void)handle;
    if (!on_device) return;
#ifdef __APPLE__
    if (device == MUSIC_SYSTEM) { Mac_ResumeSong(); return; }
#endif
    I_Mixer_ResumeSong();
}

/* F4: the song carries on from the start on the new device. */
void I_SetMusicDevice(int choice)
{
    setting = choice;
    if (!song || resolve(choice) == device) return;
    stop();
    start();
    if (playing) play();
}
