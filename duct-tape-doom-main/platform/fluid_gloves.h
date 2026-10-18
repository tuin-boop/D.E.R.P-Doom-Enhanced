#ifndef DOOM_FLUID_GLOVES_H
#define DOOM_FLUID_GLOVES_H
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
namespace doomfluid {
// Match the translation selected by each source weapon's A_GlovesColor.
// Muzzle flashes are intentionally excluded: their blue light is artwork.
inline int spriteTranslation(const std::string& name) {
    auto starts=[&](const char* prefix){return name.rfind(prefix,0)==0;};
    if(name=="PLSOI0"||starts("SH1G")||starts("PLSR")||starts("P1SG")||starts("P2SG")||starts("P1SH")||starts("PL1G")||starts("SAW1")||(starts("SH12")&&name.size()>4&&name[4]!='G'&&name[4]!='H'))return 2;
    if(starts("BFG1")||starts("SH12")||starts("PUN1")||starts("MRD1")||starts("CH1H"))return 1;
    return 0;
}
inline std::array<uint8_t,256> gloveTranslation(const uint8_t *palette,int color,bool pistol,
        std::array<float,3> shade={2,2,2},std::array<float,3> light={239,239,239}) {
    std::array<uint8_t,256> table{};for(int i=0;i<256;++i)table[i]=uint8_t(i);
    auto range=[&](int a,int b,int c,int d){for(int i=a;i<=b;++i)table[i]=uint8_t(c+(d-c)*(i-a)/std::max(1,b-a));};
    // The gun and sleeve use separate marker ranges in MR's pistol artwork.
    // Translate original indices once, never feed results through the table again.
    if(pistol){range(198,201,9,12);range(250,254,125,127);range(175,181,152,159);}
    if(color==0){range(112,126,55,79);table[127]=1;return table;}
    static const float preset[7][3]={{0,0,0},{0,0,0},{.89f,.55f,.28f},{.58f,1.45f,.55f},{1.03f,.22f,.24f},{.86f,.86f,.86f},{.31f,.51f,1.66f}};
    for(int i=112;i<=127;++i){
        float gray=(palette[i*3]*.3f+palette[i*3+1]*.59f+palette[i*3+2]*.11f)/255.f;
        int rgb[3];for(int c=0;c<3;++c){float value=color==1?40.f*(127-i)/15.f:color>=2&&color<=6?255.f*preset[color][c]*gray:2*(shade[c]+(light[c]-shade[c])*gray);rgb[c]=int(std::clamp(value,0.f,255.f));}
        int best=0,bestDistance=3*256*256;for(int j=0;j<256;++j){int distance=0;for(int c=0;c<3;++c){int delta=palette[3*j+c]-rgb[c];distance+=delta*delta;}if(distance<bestDistance){bestDistance=distance;best=j;}}table[i]=uint8_t(best);
    }return table;
}
}
#endif
