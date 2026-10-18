#ifndef DOOM_MOD_AUDIO_H
#define DOOM_MOD_AUDIO_H
#include <vector>
#include <cstdint>
namespace doommod {
struct Audio {int rate=0,channels=0;std::vector<float> pcm;int frames()const{return (int)pcm.size()/channels;}};
Audio decodeAudio(const std::vector<std::uint8_t> &bytes);
}
#endif
