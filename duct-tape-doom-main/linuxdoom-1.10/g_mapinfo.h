#ifndef DOOM_G_MAPINFO_H
#define DOOM_G_MAPINFO_H
typedef struct {
    char map[9], title[128], sky[9], music[9], levelpic[9], exitpic[9], endpic[9];
    char next[9], secret[9], episodepic[9], episodename[64], intertext[2048];
} mapinfo_t;
void G_LoadMapInfo(void);
const mapinfo_t *G_MapInfo(int episode,int map);
#endif
