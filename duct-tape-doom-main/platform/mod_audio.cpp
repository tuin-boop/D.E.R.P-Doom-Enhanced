#include "mod_audio.h"
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <memory>
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"
namespace doommod {
namespace {
uint16_t u16(const uint8_t *p){return p[0]|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t *p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
constexpr size_t maxSamples=16u*1024u*1024u;
void validate(int rate,int channels,size_t frames){
    if(rate<4000||rate>192000||channels<1||channels>2||frames==0||frames>maxSamples/(size_t)channels)
        throw std::runtime_error("Unsupported audio rate/channels/length");
}
}
Audio decodeAudio(const std::vector<uint8_t> &bytes) {
    if(bytes.size()<8||bytes.size()>64u*1024u*1024u)throw std::runtime_error("Invalid audio file size");
    Audio a;
    if(!memcmp(bytes.data(),"OggS",4)) {
        int error=0;std::unique_ptr<stb_vorbis,decltype(&stb_vorbis_close)> decoder(
            stb_vorbis_open_memory(bytes.data(),(int)bytes.size(),&error,nullptr),stb_vorbis_close);
        if(!decoder)throw std::runtime_error("Invalid OGG/Vorbis stream");
        auto info=stb_vorbis_get_info(decoder.get());a.rate=(int)info.sample_rate;a.channels=info.channels;
        auto frames=stb_vorbis_stream_length_in_samples(decoder.get());validate(a.rate,a.channels,frames);
        a.pcm.resize((size_t)frames*a.channels);
        int read=stb_vorbis_get_samples_float_interleaved(decoder.get(),a.channels,a.pcm.data(),(int)a.pcm.size());
        if(read!=(int)frames)throw std::runtime_error("Truncated OGG/Vorbis stream");
    } else if(bytes.size()>=12&&!memcmp(bytes.data(),"RIFF",4)&&!memcmp(bytes.data()+8,"WAVE",4)) {
        size_t end=(size_t)u32(bytes.data()+4)+8;if(end>bytes.size()||end<12)throw std::runtime_error("Invalid WAV RIFF size");
        const uint8_t *fmt=nullptr,*data=nullptr;size_t fmtSize=0,dataSize=0;
        for(size_t p=12;p<end;) {
            if(end-p<8)throw std::runtime_error("Truncated WAV chunk header");
            auto n=u32(bytes.data()+p+4);if(n>end-p-8)throw std::runtime_error("Truncated WAV chunk");
            if(!memcmp(bytes.data()+p,"fmt ",4)){fmt=bytes.data()+p+8;fmtSize=n;}
            if(!memcmp(bytes.data()+p,"data",4)){data=bytes.data()+p+8;dataSize=n;}
            p+=8+(size_t)n;
            // Several Doom mods omit the last odd chunk's padding byte.
            // Only permit that omission at the exact declared RIFF end.
            if(p<end)p+=n&1;
        }
        if(!fmt||fmtSize<16||!data)throw std::runtime_error("Missing WAV fmt/data");
        int format=u16(fmt),bits=u16(fmt+14),stride=u16(fmt+12);
        a.channels=u16(fmt+2);a.rate=(int)u32(fmt+4);
        if((format!=1&&format!=3)||(bits!=8&&bits!=16&&bits!=24&&bits!=32)||(format==3&&bits!=32)||stride!=a.channels*(bits/8)||!stride||dataSize%stride)
            throw std::runtime_error("Unsupported WAV sample layout");
        validate(a.rate,a.channels,dataSize/stride);a.pcm.resize(dataSize/(bits/8));
        for(size_t i=0;i<a.pcm.size();++i) {
            const uint8_t *p=data+i*(bits/8);float v=0;
            if(format==3){uint32_t raw=u32(p);memcpy(&v,&raw,4);}
            else if(bits==8)v=(int(p[0])-128)/128.0f;
            else if(bits==16)v=(int16_t)u16(p)/32768.0f;
            else if(bits==24){int32_t n=p[0]|int32_t(p[1])<<8|int32_t(p[2])<<16;if(n&0x800000)n-=0x1000000;v=n/8388608.0f;}
            else v=(int32_t)u32(p)/2147483648.0f;
            a.pcm[i]=v;
        }
    } else if(u16(bytes.data())==3) {
        a.rate=u16(bytes.data()+2);a.channels=1;size_t n=u32(bytes.data()+4);
        if(n>bytes.size()-8)throw std::runtime_error("Truncated Doom sound");validate(a.rate,1,n);a.pcm.resize(n);
        for(size_t i=0;i<n;++i)a.pcm[i]=(int(bytes[i+8])-128)/128.0f;
    } else throw std::runtime_error("Unsupported audio format");
    for(float &v:a.pcm){if(!std::isfinite(v))throw std::runtime_error("Non-finite audio sample");v=std::clamp(v,-1.0f,1.0f);}
    return a;
}
}
