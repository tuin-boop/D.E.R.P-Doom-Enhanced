#ifndef DOOM_MOD_SOUNDBANK_H
#define DOOM_MOD_SOUNDBANK_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const float *pcm;
    int frames,rate,channels,limit;
    unsigned group;
    float volume;
    const char *fallback_lump;
} mod_sound_view;
/* The game thread owns the bank. Returned PCM remains valid until Clear;
 * destroy/stop the audio stream before clearing it. No I/O in audio callback. */
int ModSounds_AddArchive(const char *path,char *error,int error_size);
int ModSounds_Get(const char *logical,unsigned choice,mod_sound_view *out,char *error,int error_size);
int ModSounds_Has(const char *logical);
void ModSounds_UseAddon(int enabled);
void ModSounds_Clear(void);
#ifdef __cplusplus
}
#endif
#endif
