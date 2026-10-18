#include "mod_textures.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <climits>
#include <cctype>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#include "../third_party/stb/stb_image.h"
namespace doommod {
namespace {
std::string lower(std::string s){for(auto &c:s)if(c>='A'&&c<='Z')c+=32;return s;}
uint16_t u16(const uint8_t*p){return p[0]|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
int nearest(const uint8_t*p,int r,int g,int b){int best=0,distance=INT_MAX;for(int i=0;i<256;++i){int x=p[3*i]-r,y=p[3*i+1]-g,z=p[3*i+2]-b;int d=x*x+y*y+z*z;if(d<distance){distance=d;best=i;}}return best;}
void dimensions(int w,int h){if(w<1||h<1||w>4096||h>4096||uint64_t(w)*h>4*1024*1024)throw std::runtime_error("Mod image dimensions exceed limits");}
struct Tokens {
    const std::string&s;size_t p=0;unsigned line=1;
    [[noreturn]]void fail(const std::string&m){throw std::runtime_error("TEXTURES line "+std::to_string(line)+": "+m);}
    void space(){while(p<s.size()){if(s[p]=='\n'){++line;++p;}else if(s[p]==' '||s[p]=='\t'||s[p]=='\r')++p;else if(s.compare(p,2,"//")==0){while(p<s.size()&&s[p]!='\n')++p;}else if(s.compare(p,2,"/*")==0){p+=2;while(p<s.size()&&s.compare(p,2,"*/")){if(s[p++]=='\n')++line;}if(p==s.size())fail("unterminated comment");p+=2;}else break;}}
    bool end(){space();return p==s.size();}
    std::string next(){space();if(p==s.size())fail("unexpected end");if(s[p]=='{'||s[p]=='}'||s[p]==',')return std::string(1,s[p++]);std::string v;if(s[p]=='"'){++p;while(p<s.size()&&s[p]!='"')v+=s[p++];if(p==s.size())fail("unterminated string");++p;}else{while(p<s.size()&&!std::isspace((unsigned char)s[p])&&s[p]!=','&&s[p]!='{'&&s[p]!='}')v+=s[p++];}return v;}
    bool take(const char*v){size_t old=p;unsigned oldline=line;if(!end()&&lower(next())==v)return true;p=old;line=oldline;return false;}
    void expect(const char*v){if(!take(v))fail(std::string("expected ")+v);}
    float number(){auto s=next();char*e;float f=std::strtof(s.c_str(),&e);if(e==s.c_str()||*e||!std::isfinite(f)||std::abs(f)>65536)fail("invalid number");return f;}
    int integer(){float f=number();if(std::floor(f)!=f)fail("expected integer");return int(f);}
};
}
ModImage decodeModImage(const std::vector<uint8_t>&d,const uint8_t*pal){
    ModImage out;if(d.size()<8||d.size()>64*1024*1024)throw std::runtime_error("Invalid mod image size");
    if(!std::memcmp(d.data(),"\x89PNG\r\n\x1a\n",8)){
        int ch;if(!stbi_info_from_memory(d.data(),int(d.size()),&out.width,&out.height,&ch))throw std::runtime_error("Invalid PNG header");dimensions(out.width,out.height);
        auto rgba=stbi_load_from_memory(d.data(),int(d.size()),&out.width,&out.height,&ch,4);if(!rgba)throw std::runtime_error("Invalid PNG data");
        out.pixels.resize(size_t(out.width)*out.height*2);
        for(size_t i=0;i<out.pixels.size()/2;++i){out.pixels[2*i]=uint8_t(nearest(pal,rgba[4*i],rgba[4*i+1],rgba[4*i+2]));out.pixels[2*i+1]=rgba[4*i+3];}stbi_image_free(rgba);
        // ZDoom's ancillary grAb chunk stores signed big-endian sprite offsets.
        for(size_t p=8;p+12<=d.size();){uint32_t len=uint32_t(d[p])<<24|uint32_t(d[p+1])<<16|uint32_t(d[p+2])<<8|d[p+3];if(len>d.size()-p-12)throw std::runtime_error("Truncated PNG chunk");if(len==8&&!std::memcmp(d.data()+p+4,"grAb",4)){auto be=[&](size_t at){return int32_t(uint32_t(d[at])<<24|uint32_t(d[at+1])<<16|uint32_t(d[at+2])<<8|d[at+3]);};out.left=be(p+8);out.top=be(p+12);}p+=len+12;}
        return out;
    }
    out.width=u16(d.data());out.height=u16(d.data()+2);out.left=int16_t(u16(d.data()+4));out.top=int16_t(u16(d.data()+6));dimensions(out.width,out.height);
    if(size_t(out.width)*4>d.size()-8)throw std::runtime_error("Truncated patch columns");out.pixels.resize(size_t(out.width)*out.height*2);
    for(int x=0;x<out.width;++x){size_t p=u32(d.data()+8+4*x);int previous=-1;unsigned posts=0;
        for(;;){if(p>=d.size()||++posts>8192)throw std::runtime_error("Unterminated patch column");int y=d[p++];if(y==255)break;if(p+2>d.size())throw std::runtime_error("Truncated patch post");int n=d[p++];++p;if(size_t(n)+1>d.size()-p)throw std::runtime_error("Truncated patch pixels");if(y<=previous)y+=previous;previous=y;for(int j=0;j<n;++j)if(y+j<out.height){size_t at=(size_t(y+j)*out.width+x)*2;out.pixels[at]=d[p+j];out.pixels[at+1]=255;}p+=n+1;}
    }return out;
}
TextureBank::TextureBank(const uint8_t*p,std::function<std::vector<uint8_t>(const std::string&)> f):fallback(std::move(f)){if(!p)throw std::runtime_error("Missing palette");std::copy(p,p+768,palette.begin());}
void TextureBank::parse(const std::string&text){
    if(text.size()>4*1024*1024)throw std::runtime_error("TEXTURES too large");Tokens t{text};
    while(!t.end()){auto kind=lower(t.next());if(kind=="notrim"){untrimmed.insert(lower(t.next()));continue;}if(kind!="sprite"&&kind!="texture"&&kind!="graphic")t.fail("unsupported definition: "+kind);
        Definition d;d.optional=t.take("optional");auto name=lower(t.next());t.expect(",");d.width=t.integer();t.expect(",");d.height=t.integer();dimensions(d.width,d.height);t.expect("{");
        while(!t.take("}")){auto op=lower(t.next());if(op=="offset"){d.left=t.integer();t.expect(",");d.top=t.integer();}else if(op=="xscale"||op=="yscale"){float v=t.number();if(v<.001f||v>1000)t.fail("invalid scale");(op=="xscale"?d.scaleX:d.scaleY)=v;}else if(op=="patch"){
            Patch p;p.name=lower(t.next());t.expect(",");p.x=t.integer();t.expect(",");p.y=t.integer();if(t.take("{"))while(!t.take("}")){auto prop=lower(t.next());if(prop=="flipx")p.flipX=true;else if(prop=="flipy")p.flipY=true;else if(prop=="rotate"){p.rotation=t.integer();if(p.rotation%90)t.fail("rotation must be a multiple of 90");p.rotation=(p.rotation%360+360)%360;}else if(prop=="alpha"){p.alpha=t.number();if(p.alpha<0||p.alpha>1)t.fail("alpha outside 0..1");}else if(prop=="style"){p.style=lower(t.next());if(p.style!="copy"&&p.style!="translucent"&&p.style!="overlay")t.fail("unsupported patch style");}else if(prop=="translation"){do{p.translations.push_back(t.next());}while(t.take(","));}else if(prop=="blend"){auto color=t.next();unsigned c;if(color.size()!=7||color[0]!='#'||std::sscanf(color.c_str()+1,"%x",&c)!=1)t.fail("invalid blend color");p.blend={int(c>>16)&255,int(c>>8)&255,int(c)&255};t.expect(",");p.blendAmount=t.number();if(p.blendAmount<0||p.blendAmount>1)t.fail("invalid blend amount");}else t.fail("unsupported patch property: "+prop);}
            if(d.patches.size()>=4096)t.fail("too many patches");d.patches.push_back(std::move(p));
        }else t.fail("unsupported texture property: "+op);}
        if(definitions.size()>=32768)t.fail("too many definitions");definitions[name]=std::move(d);
    }
}
void TextureBank::addArchive(const std::string&path){
    if(!cache.empty())throw std::runtime_error("Cannot change texture bank after rendering starts");auto archive=std::make_shared<Archive>(path);auto oldDefs=definitions;auto oldRes=resources;
    try{for(const auto&name:archive->names()){if(name.rfind("sprites/",0)==0||name.rfind("patches/",0)==0||name.rfind("graphics/",0)==0){auto slash=name.find_last_of('/'),dot=name.find_last_of('.');if(dot!=std::string::npos&&dot>slash){auto ext=name.substr(dot);if(ext==".lmp"||ext==".png")resources[name.substr(slash+1,dot-slash-1)]={archives.size(),name};}}if(name.rfind("textures",0)==0&&(name=="textures"||name.substr(name.size()-4)==".txt"))parse(archive->text(name));}archives.push_back(std::move(archive));}catch(...){definitions=std::move(oldDefs);resources=std::move(oldRes);throw;}
}
ModImage TextureBank::compose(const std::string&name){
    auto def=definitions.find(name);
    if(def==definitions.end()){auto resource=resources.find(name);if(resource!=resources.end())return decodeModImage(archives[resource->second.first]->read(resource->second.second),palette.data());if(fallback){auto bytes=fallback(name);if(!bytes.empty())return decodeModImage(bytes,palette.data());}throw std::runtime_error("Missing texture resource: "+name);}
    const auto&d=def->second;ModImage out;out.width=d.width;out.height=d.height;out.left=d.left;out.top=d.top;out.scaleX=d.scaleX;out.scaleY=d.scaleY;out.pixels.resize(size_t(d.width)*d.height*2);
    for(const auto&p:d.patches){
        // A definition may deliberately reuse its original raw patch name.
        ModImage own;const ModImage*source;
        if(p.name==name&&resources.count(name)){auto r=resources.at(name);own=decodeModImage(archives[r.first]->read(r.second),palette.data());source=&own;}else source=&image(p.name);
        const auto&s=*source;std::array<uint8_t,256> translation;for(int i=0;i<256;++i)translation[i]=i;
        for(const auto&rule:p.translations){int a,b,c,e,used=0;float r0,g0,b0,r1,g1,b1;bool desaturate=false;
            if(std::sscanf(rule.c_str(),"%d:%d=%d:%d%n",&a,&b,&c,&e,&used)==4&&used==int(rule.size())){if(a<0||a>255||b<a||b>255||c<0||c>255||e<0||e>255)throw std::runtime_error("Invalid palette translation");for(int i=a;i<=b;++i)translation[i]=uint8_t(c+(e-c)*(i-a)/std::max(1,b-a));}
            else {used=0;int n=std::sscanf(rule.c_str(),"%d:%d=[%f,%f,%f]:[%f,%f,%f]%n",&a,&b,&r0,&g0,&b0,&r1,&g1,&b1,&used);if(n!=8){desaturate=true;used=0;n=std::sscanf(rule.c_str(),"%d:%d=%%[%f,%f,%f]:[%f,%f,%f]%n",&a,&b,&r0,&g0,&b0,&r1,&g1,&b1,&used);}if(n!=8||used!=int(rule.size())||a<0||a>255||b<a||b>255)throw std::runtime_error("Unsupported palette translation: "+rule);for(float v:{r0,g0,b0,r1,g1,b1})if(!std::isfinite(v)||v<0||v>255)throw std::runtime_error("Invalid translation color");for(int i=a;i<=b;++i){float f=desaturate?(palette[3*i]*.3f+palette[3*i+1]*.59f+palette[3*i+2]*.11f)/255:float(i-a)/std::max(1,b-a);float scale=desaturate?255.0f:1.0f;translation[i]=nearest(palette.data(),int(std::clamp((r0+(r1-r0)*f)*scale,0.f,255.f)),int(std::clamp((g0+(g1-g0)*f)*scale,0.f,255.f)),int(std::clamp((b0+(b1-b0)*f)*scale,0.f,255.f)));}}
        }
        for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x){size_t src=(size_t(y)*s.width+x)*2;float alpha=s.pixels[src+1]/255.f*(p.style=="translucent"?p.alpha:1.f);if(alpha==0)continue;int dx=p.flipX?s.width-1-x:x,dy=p.flipY?s.height-1-y:y;int rx=dx,ry=dy;if(p.rotation==90){rx=s.height-1-dy;ry=dx;}else if(p.rotation==180){rx=s.width-1-dx;ry=s.height-1-dy;}else if(p.rotation==270){rx=dy;ry=s.width-1-dx;}rx+=p.x;ry+=p.y;if(rx<0||rx>=out.width||ry<0||ry>=out.height)continue;size_t dst=(size_t(ry)*out.width+rx)*2;int color=translation[s.pixels[src]],back=out.pixels[dst];float backAlpha=out.pixels[dst+1]/255.f;float resulting=alpha+backAlpha*(1-alpha);int rgb[3];for(int k=0;k<3;++k){float front=palette[3*color+k]*(1-p.blendAmount)+p.blend[k]*p.blendAmount;rgb[k]=int((front*alpha+palette[3*back+k]*backAlpha*(1-alpha))/resulting+.5f);}out.pixels[dst]=nearest(palette.data(),rgb[0],rgb[1],rgb[2]);out.pixels[dst+1]=uint8_t(resulting*255+.5f);}
    }return out;
}
const ModImage&TextureBank::image(const std::string&raw){auto name=lower(raw);auto found=cache.find(name);if(found!=cache.end())return found->second;if(active.size()>=64||!active.insert(name).second)throw std::runtime_error("Cyclic texture composition: "+name);try{auto out=compose(name);if(out.pixels.size()>128*1024*1024-cachedBytes)throw std::runtime_error("Texture cache exceeds limit");cachedBytes+=out.pixels.size();active.erase(name);return cache.emplace(name,std::move(out)).first->second;}catch(...){active.erase(name);throw;}}
std::vector<std::string>TextureBank::names()const{std::set<std::string>all;for(auto&r:resources)all.insert(r.first);for(auto&d:definitions)all.insert(d.first);return {all.begin(),all.end()};}
bool TextureBank::noTrim(const std::string&name)const{return untrimmed.count(lower(name))!=0;}
bool TextureBank::optional(const std::string&name)const{auto d=definitions.find(lower(name));return d!=definitions.end()&&d->second.optional;}
}

