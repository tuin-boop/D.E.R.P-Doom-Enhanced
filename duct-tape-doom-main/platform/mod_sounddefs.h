#ifndef DOOM_MOD_SOUNDDEFS_H
#define DOOM_MOD_SOUNDDEFS_H
#include <map>
#include <string>
#include <vector>
#include <cstdint>
namespace doommod {
struct SoundDefinition {
    std::string resource;
    std::vector<std::string> alternatives;
    float volume=1;
    int limit=0;
};
struct ResolvedSound {std::string resource;float volume=1;int limit=0;};
class SoundDefinitions {
    std::map<std::string,SoundDefinition> definitions;
public:
    // Apply a later SNDINFO after earlier packs, preserving load order.
    // Supported grammar: mappings, $random, $alias, $volume, $limit.
    // Unknown directives fail explicitly instead of shifting token alignment.
    void parse(const std::string &text);
    ResolvedSound resolve(const std::string &logical,std::uint32_t choice=0) const;
    const std::map<std::string,SoundDefinition> &all() const {return definitions;}
};
}
#endif
