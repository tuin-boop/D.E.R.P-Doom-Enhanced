/* The music devices behind DOOM's I_*Music calls, chosen in i_music.c. */
#ifndef DOOM_I_MUSIC_H
#define DOOM_I_MUSIC_H

/* Rendered in the SDL mixer; these take the audio lock (i_sdl_sound.c). */
/* soundfont: through the bank in use, else the OPL. */
int I_Mixer_RegisterSong(void *data, int soundfont);
void I_Mixer_UnRegisterSong(void);
void I_Mixer_PlaySong(int looping);
void I_Mixer_StopSong(void);
void I_Mixer_PauseSong(void);
void I_Mixer_ResumeSong(void);
void I_Mixer_SetMusicVolume(int volume);
/* Reads a .sf2 bank and puts it in use; 0 if it can't be read. */
int I_Mixer_LoadSoundFont(const char *path);

#ifdef __APPLE__
/* The Mac's own General MIDI synthesizer (i_mac_music.c). */
int Mac_RegisterSong(void *data);
void Mac_UnRegisterSong(void);
void Mac_PlaySong(int looping);
void Mac_StopSong(void);
void Mac_PauseSong(void);
void Mac_ResumeSong(void);
void Mac_SetMusicVolume(int volume);
#endif
#endif
