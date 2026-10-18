#include "../platform/mod_audio.h"
#include "../platform/mod_archive.h"
#include "../platform/mod_soundbank.h"
#include <cstdio>
#include <cmath>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
int main(int argc,char **argv) {
    try {
        if(argc>1) {
            size_t total=0;double seconds=0;
            for(int i=1;i<argc;++i) {
                doommod::Archive a(argv[i]);for(auto name:a.names()) {
                    if(name.size()<4)continue;auto ext=name.substr(name.size()-4);
                    if(ext!=".ogg"&&ext!=".wav")continue;
                    try{auto sample=doommod::decodeAudio(a.read(name));CHECK(sample.frames()>0);seconds+=sample.frames()/(double)sample.rate;++total;}
                    catch(const std::exception &e){throw std::runtime_error(name+": "+e.what());}
                }
            }
            char error[512];for(int i=1;i<argc;++i)CHECK(ModSounds_AddArchive(argv[i],error,sizeof(error)));
            mod_sound_view view;CHECK(ModSounds_Get("MRIntW/HQ/PistolFire",0,&view,error,sizeof(error)));
            CHECK(view.pcm&&view.frames>0&&view.volume>0);const float *retained=view.pcm;
            CHECK(ModSounds_Get("MRIntW/HQ/ShotgunFire",0,&view,error,sizeof(error)));CHECK(std::abs(view.volume-.6f)<.0001f);
            CHECK(ModSounds_Get("MRIntW/HQ/PistolFire",0,&view,error,sizeof(error)));CHECK(retained==view.pcm);
            ModSounds_Clear();
            printf("Decoded %zu actual Fluid Weapons OGG/WAV assets, %.2f seconds; named sound bank resolved HQ events\n",total,seconds);return 0;
        }
        std::vector<uint8_t> d={3,0,0x11,0x2b,3,0,0,0,0,128,255};
        auto a=doommod::decodeAudio(d);CHECK(a.rate==11025&&a.channels==1&&a.frames()==3);CHECK(a.pcm[0]==-1&&a.pcm[1]==0&&a.pcm[2]>.99f);
        bool bad=false;d.pop_back();try{doommod::decodeAudio(d);}catch(const std::runtime_error &){bad=true;}CHECK(bad);
        std::vector<uint8_t> w={'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',2,0,0,0,0,0x80};
        a=doommod::decodeAudio(w);CHECK(a.rate==44100&&a.pcm.size()==1&&a.pcm[0]==-1);
        for(size_t n=0;n<w.size();++n){bad=false;auto trunc=std::vector<uint8_t>(w.begin(),w.begin()+n);try{doommod::decodeAudio(trunc);}catch(const std::runtime_error &){bad=true;}CHECK(bad);}
        puts("Audio decode and truncated-input checks passed");return 0;
    }catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}
}
