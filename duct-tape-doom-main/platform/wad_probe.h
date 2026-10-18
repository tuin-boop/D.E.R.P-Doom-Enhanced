#ifndef DOOM_WAD_PROBE_H
#define DOOM_WAD_PROBE_H

typedef struct {
    int is_iwad;
    int episode_maps;
    int numbered_maps;
    int full_game;
    int has_palette; /* PLAYPAL: missing from DOOM 64 and other non-DOOM games. */
} doom_wad_info;

/* Check the directory before offering a file in the launcher. */
#ifdef __cplusplus
extern "C" {
#endif
int DOOM_ProbeWad(const char *path, doom_wad_info *info);
#ifdef __cplusplus
}
#endif
#endif
