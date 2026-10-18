/* Data-free checks for monitor glass detection (platform/screen_glass.h). */
#include "../platform/screen_glass.h"
#include <stdio.h>

static void check(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
int main(void)
{
    enum { W=64, H=64, BLACK=0, GREY=1, GREEN=2 };
    unsigned char palette[768]={0};
    palette[3]=palette[4]=palette[5]=100;
    palette[6]=40; palette[7]=200; palette[8]=40;
    static unsigned char pixels[W*H*2], glass[W*H], frame[W*H*4];
    for (int i=0;i<W*H;++i) { pixels[2*i]=GREY; pixels[2*i+1]=255; }
    /* A black screen with green readout lines, and a vent grille of alternating rows. */
    for (int y=10;y<26;++y) for (int x=10;x<34;++x)
        pixels[2*(y*W+x)]=(y%4==1 && x%3) ? GREEN : BLACK;
    for (int y=40;y<52;++y) for (int x=40;x<56;++x)
        pixels[2*(y*W+x)]=y%2 ? BLACK : GREY;
    check(doom_screen_glass(W,H,pixels,palette,glass,frame)==1, "Exactly the screen must be found");
    const unsigned char *f=&frame[(17*W+21)*4];
    check(f[0]==11 && f[1]==7 && f[2]==24 && f[3]==16, "Frame must locate the pixel on its screen");
    check(glass[17*W+21]!=DOOM_GLASS_NONE && glass[10*W+10]!=DOOM_GLASS_NONE, "Screen pixels must be glass");
    check(glass[17*W+21]>glass[10*W+10], "Glass must bulge toward the middle");
    check(glass[5*W+5]==DOOM_GLASS_NONE && glass[9*W+21]==DOOM_GLASS_NONE, "Housing must stay matte");
    check(glass[45*W+47]==DOOM_GLASS_NONE, "Vent grilles must not become glass");
    check(doom_screen_texture("COMPSTA1",0) && doom_screen_texture("CONS1_5",1) &&
          !doom_screen_texture("STARTAN3",0) && !doom_screen_texture("COMPSTA1",1), "Screen texture names");
    return 0;
}
