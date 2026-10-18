#include "mod_soundbank.h"
#include "mod_archive.h"
#include "mod_sounddefs.h"
#include "mod_audio.h"
#include <map>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
std::vector<std::unique_ptr<doommod::Archive>> archives;
doommod::SoundDefinitions definitions,baseDefinitions;
bool useAddon=true;
const doommod::SoundDefinitions &activeDefinitions(){return useAddon?definitions:baseDefinitions;}
struct Cached {doommod::Audio audio;std::string lump;};
std::map<std::string,std::unique_ptr<Cached>> cache;
size_t cachedBytes=0;
std::map<std::string,unsigned> groups;
void errorText(char *out,int size,const char *message){if(out&&size>0)std::snprintf(out,(size_t)size,"%s",message);}
}
extern "C" int ModSounds_AddArchive(const char *path,char *error,int size) {
    try {
        if(archives.size()>=18)throw std::runtime_error("Too many sound archives");
        if(!cache.empty())throw std::runtime_error("Cannot change sound archives during playback");
        auto archive=std::make_unique<doommod::Archive>(path);
        if(!archive->contains("sndinfo.txt"))throw std::runtime_error("Sound pack has no SNDINFO.txt");
        auto candidate=definitions;candidate.parse(archive->text("sndinfo.txt"));
        archives.push_back(std::move(archive));definitions=std::move(candidate);if(archives.size()==1)baseDefinitions=definitions;return 1;
    }catch(const std::exception &e){errorText(error,size,e.what());return 0;}
}
extern "C" int ModSounds_Has(const char *logical) {
    try {activeDefinitions().resolve(logical);return 1;}catch(...){return 0;}
}
extern "C" int ModSounds_Get(const char *logical,unsigned choice,mod_sound_view *out,char *error,int size) {
    if(!out)return 0;std::memset(out,0,sizeof(*out));
    try {
        auto resolved=activeDefinitions().resolve(logical,choice);out->volume=resolved.volume;out->limit=resolved.limit;
        auto groupKey=doommod::Archive::normalize(logical);
        auto group=groups.find(groupKey);
        if(group==groups.end())group=groups.emplace(groupKey,(unsigned)groups.size()+1).first;
        out->group=group->second;
        if(resolved.resource.empty())return 1; // Explicitly silent logical sound.
        auto key=doommod::Archive::normalize(resolved.resource);
        auto cacheKey=(useAddon?"addon:":"base:")+key;
        auto found=cache.find(cacheKey);
        if(found==cache.end()) {
            auto value=std::make_unique<Cached>();bool loaded=false;
            for(size_t n=useAddon?archives.size():std::min(size_t(1),archives.size());n>0&&!loaded;--n) {
                auto &archive=archives[n-1];
                for(const char *suffix:{"",".ogg",".wav"}) {
                    auto name=key+suffix;if(!archive->contains(name))continue;
                    value->audio=doommod::decodeAudio(archive->read(name));loaded=true;break;
                }
            }
            if(!loaded) {
                if(key.size()<=8&&key.find('/')==std::string::npos&&key.find('.')==std::string::npos)value->lump=key;
                else throw std::runtime_error("Missing named sound resource: "+key);
            }
            size_t bytes=value->audio.pcm.size()*sizeof(float);
            if(bytes>128u*1024u*1024u-cachedBytes)throw std::runtime_error("Decoded sound cache exceeds 128 MiB");
            found=cache.emplace(cacheKey,std::move(value)).first;cachedBytes+=bytes;
        }
        const auto &v=*found->second;
        if(!v.lump.empty())out->fallback_lump=v.lump.c_str();
        else {out->pcm=v.audio.pcm.data();out->frames=v.audio.frames();out->rate=v.audio.rate;out->channels=v.audio.channels;}
        return 1;
    }catch(const std::exception &e){errorText(error,size,e.what());return 0;}
}
extern "C" void ModSounds_UseAddon(int enabled){useAddon=enabled!=0;}
extern "C" void ModSounds_Clear(void){cache.clear();groups.clear();cachedBytes=0;definitions=baseDefinitions=doommod::SoundDefinitions{};archives.clear();}
