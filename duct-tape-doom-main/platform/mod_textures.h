#ifndef DOOM_MOD_TEXTURES_H
#define DOOM_MOD_TEXTURES_H
#include "mod_archive.h"
#include <array>
#include <functional>
#include <map>
#include <set>
namespace doommod {
// CPU resource compositor. Pixel pairs are palette index and coverage, matching
// Duct Tape Doom's existing image upload and weapon lighting input.
struct ModImage {
    int width=0,height=0,left=0,top=0;
    float scaleX=1,scaleY=1;
    std::vector<uint8_t> pixels;
};
ModImage decodeModImage(const std::vector<uint8_t>& data,const uint8_t *palette);
class TextureBank {
    struct Patch {
        std::string name,style="copy"; int x=0,y=0,rotation=0;
        bool flipX=false,flipY=false; float alpha=1,blendAmount=0;
        std::array<int,3> blend{};
        std::vector<std::string> translations;
    };
    struct Definition {
        int width=0,height=0,left=0,top=0; float scaleX=1,scaleY=1;
        bool optional=false; std::vector<Patch> patches;
    };
    std::vector<std::shared_ptr<Archive>> archives;
    std::map<std::string,std::pair<size_t,std::string>> resources;
    std::map<std::string,Definition> definitions;
    std::map<std::string,ModImage> cache;
    std::set<std::string> active;
    std::set<std::string> untrimmed;
    std::array<uint8_t,768> palette{};
    std::function<std::vector<uint8_t>(const std::string&)> fallback;
    size_t cachedBytes=0;
    void parse(const std::string&);
    ModImage compose(const std::string&);
public:
    TextureBank(const uint8_t *palette,std::function<std::vector<uint8_t>(const std::string&)> fallback={});
    void addArchive(const std::string& path);
    const ModImage& image(const std::string& name);
    std::vector<std::string> names() const;
    bool optional(const std::string& name) const;
    bool noTrim(const std::string& name) const;
};
}
#endif
