#include "../platform/mod_textures.h"
#include "../platform/fluid_weapons.h"
#include "../platform/weapon_switch.h"
#include "../platform/fluid_gloves.h"
#include "../platform/weapon_lighting.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <cstring>
#include <stdexcept>
using namespace doommod;
static void check(bool good,const char*why){if(!good)throw std::runtime_error(why);}
static uint32_t word(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
int main(int argc,char**argv){try{
    std::array<uint8_t,768>pal{};for(int i=0;i<256;++i)pal[3*i]=pal[3*i+1]=pal[3*i+2]=i;
    std::vector<uint8_t> patch={1,0,2,0,2,0,255,255,12,0,0,0,0,2,0,32,48,0,255};
    auto im=decodeModImage(patch,pal.data());check(im.width==1&&im.height==2&&im.left==2&&im.top==-1,"patch geometry");check(im.pixels==std::vector<uint8_t>({32,255,48,255}),"patch decode");patch.pop_back();bool rejected=false;try{decodeModImage(patch,pal.data());}catch(...){rejected=true;}check(rejected,"unterminated patch accepted");
    if(argc==3){std::ifstream f(argv[1],std::ios::binary);check(bool(f),"IWAD missing");std::vector<uint8_t>wad((std::istreambuf_iterator<char>(f)),{});check(wad.size()>=12,"bad IWAD");uint32_t count=word(wad.data()+4),dir=word(wad.data()+8);check(uint64_t(dir)+uint64_t(count)*16<=wad.size(),"bad directory");std::map<std::string,std::vector<uint8_t>>lumps;for(uint32_t n=0;n<count;++n){const auto*p=wad.data()+dir+n*16;uint32_t at=word(p),len=word(p+4);check(uint64_t(at)+len<=wad.size(),"bad lump");std::string name((const char*)p+8,8);name.resize(name.find('\0')==std::string::npos?8:name.find('\0'));for(auto&c:name)if(c>='A'&&c<='Z')c+=32;lumps[name]={wad.begin()+at,wad.begin()+at+len};}check(lumps["playpal"].size()>=768,"palette missing");TextureBank bank(lumps["playpal"].data(),[&](const std::string&name){return lumps[name];});bank.addArchive(argv[2]);if(const char*dir=std::getenv("DOOM_DUMP_FLUID")){for(const char*name:{"SH1GH0","PL1GA0","PL1GB0","PLSRA0","PLSRB0","PLSRD0","PLSOI0","PLSOA0","BFG1A0","BFGMA0"}){
const auto&im=bank.image(name);auto px=im.pixels;
auto trans=doomfluid::gloveTranslation(lumps["playpal"].data(),0,doomfluid::spriteTranslation(name)==2);
for(size_t n=0;n<px.size();n+=2)if(doomfluid::spriteTranslation(name)!=0)px[n]=trans[px[n]];
for(int lit=0;lit<2;++lit){auto pixels=lit?doom_weapon_delight(im.width,im.height,px.data(),lumps["playpal"].data()).pixels:px;
std::ofstream o(std::string(dir)+"/"+name+(lit?"-relit.bmp":"-raw.bmp"),std::ios::binary);
unsigned char header[54]={66,77};auto put=[&](int at,unsigned v){for(int b=0;b<4;++b)header[at+b]=v>>(8*b);};put(2,54+im.width*im.height*4);put(10,54);put(14,40);put(18,im.width);put(22,im.height);header[26]=1;header[28]=32;o.write((char*)header,54);
for(int y=im.height-1;y>=0;--y)for(int x=0;x<im.width;++x){int at=(y*im.width+x)*2,c=pixels[at];char rgb[4]={(char)lumps["playpal"][3*c+2],(char)lumps["playpal"][3*c+1],(char)lumps["playpal"][3*c],(char)pixels[at+1]};o.write(rgb,4);}
}}
}
int good=0,optional=0,external=0,failed=0;for(auto&name:bank.names()){try{auto&i=bank.image(name);check(!i.pixels.empty(),"empty image");++good;}catch(const std::exception&e){if(name=="mr_wma1" && std::string(e.what())=="Missing texture resource: pista0"){++external;}else if(bank.optional(name)){++optional;}else{++failed;std::cerr<<name<<": "<<e.what()<<"\n";}}}std::cout<<good<<" images composed, "<<optional<<" optional absent, "<<external<<" known external menu icon, "<<failed<<" failures\n";check(failed==0,"actual package textures failed"); for(int weapon:{0,1,2,3,4,5,6,7,8}){doomfluid::Weapons animation;animation.reset(weapon);for(const auto&layer:animation.layers(1))bank.image(layer.sprite);animation.attack(weapon);animation.shot(weapon);for(int tic=0;tic<500;++tic){if(weapon==3&&tic<400&&tic%4==0)animation.shot(weapon);for(const auto&layer:animation.layers(.5f))bank.image(layer.sprite);for(const auto&layer:animation.layers(.5f,false))bank.image(layer.sprite);animation.tick(weapon);}}for(int weapon=0;weapon<9;++weapon){doomfluid::Weapons a;a.reset(weapon);for(int tic=0;tic<400;++tic){a.input(tic<40,tic>=200&&tic<210);a.tick(weapon);for(bool bare:{false,true})for(const auto&layer:a.layers(.5f,bare))bank.image(layer.sprite);}}for(const auto&pose:doomweapon::switchPoses){bank.image(pose.sprite);}auto&pistol=bank.image("P1SGA0");check(pistol.width==120&&pistol.height==138&&pistol.left==-133&&pistol.top==-76,"pistol composition geometry");}
    std::cout<<"Texture tests passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}



