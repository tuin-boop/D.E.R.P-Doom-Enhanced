#include "mod_sounddefs.h"
#include <cmath>
#include <set>
#include <stdexcept>
#include <cstdlib>
namespace doommod {
namespace {
std::string lower(std::string s){for(char &c:s)if(c>='A'&&c<='Z')c+=32;return s;}
struct Tokens {
    const std::string &s;size_t p=0;unsigned line=1;
    [[noreturn]] void fail(const std::string &m) const {throw std::runtime_error("SNDINFO line "+std::to_string(line)+": "+m);}
    void space() {
        while(p<s.size()) {
            if(s[p]=='\n'){++line;++p;continue;}
            if(s[p]==' '||s[p]=='\r'||s[p]=='\t'){++p;continue;}
            if(s.compare(p,2,"//")==0){while(p<s.size()&&s[p]!='\n')++p;continue;}
            if(s.compare(p,2,"/*")==0){p+=2;while(p<s.size()&&s.compare(p,2,"*/")){if(s[p]=='\n')++line;++p;}if(p==s.size())fail("unterminated comment");p+=2;continue;}
            break;
        }
    }
    bool end(){space();return p==s.size();}
    std::string next() {
        space();if(p==s.size())fail("unexpected end of input");
        if(s[p]=='{'||s[p]=='}')return std::string(1,s[p++]);
        std::string out;
        if(s[p]=='"') {
            ++p;while(p<s.size()&&s[p]!='"') {
                if(s[p]=='\n'||s[p]=='\r')fail("newline inside quoted name");
                if(s[p]=='\\'&&p+1<s.size()&&(s[p+1]=='"'||s[p+1]=='\\'))++p;
                out+=s[p++];
            }
            if(p==s.size())fail("unterminated quoted name");++p;return out;
        }
        while(p<s.size()&&s[p]!=' '&&s[p]!='\t'&&s[p]!='\r'&&s[p]!='\n'&&s[p]!='{'&&s[p]!='}') {
            if(s.compare(p,2,"//")==0||s.compare(p,2,"/*")==0)break;
            out+=s[p++];
        }
        if(out.empty())fail("expected a name");return out;
    }
    double number() {std::string n=next();char *end=nullptr;double v=std::strtod(n.c_str(),&end);if(end==n.c_str()||*end||!std::isfinite(v))fail("invalid number: "+n);return v;}
    std::string name(){auto n=lower(next());if(n.empty()||n=="{"||n=="}"||n[0]=='$')fail("invalid logical sound name");return n;}
};
}
void SoundDefinitions::parse(const std::string &text) {
    if(text.size()>4u*1024u*1024u)throw std::runtime_error("SNDINFO exceeds text limit");
    auto parsed=definitions; // A malformed later pack must not partially overwrite earlier ones.
    Tokens t{text};
    while(!t.end()) {
        auto token=lower(t.next());
        if(token=="$random") {
            auto name=t.name();auto &d=parsed[name];d.resource.clear();d.alternatives.clear();
            if(t.next()!="{")t.fail("expected '{' after $random name");
            while(true) {
                auto value=lower(t.next());if(value=="}")break;
                if(value.empty()||value=="{"||value[0]=='$')t.fail("invalid $random alternative");
                if(d.alternatives.size()>=4096)t.fail("too many random alternatives");
                d.alternatives.push_back(value);
            }
            if(d.alternatives.empty())t.fail("empty $random group");
        } else if(token=="$alias") {
            auto name=t.name(),target=t.name();auto &d=parsed[name];d.resource.clear();d.alternatives={target};
        } else if(token=="$volume") {
            auto name=t.name();double value=t.number();
            if(value<0||value>1)t.fail("volume outside 0..1");parsed[name].volume=(float)value;
        } else if(token=="$limit") {
            auto name=t.name();double value=t.number();
            if(value<0||value>65535||std::floor(value)!=value)t.fail("invalid concurrency limit");parsed[name].limit=(int)value;
        } else {
            if(token.empty()||token=="{"||token=="}"||token[0]=='$')t.fail("unsupported directive or invalid name: "+token);
            auto &d=parsed[token];d.resource=t.next();d.alternatives.clear();
            if(d.resource=="{"||d.resource=="}"||(!d.resource.empty()&&d.resource[0]=='$'))t.fail("expected resource path");
        }
        if(parsed.size()>100000)t.fail("too many sounds");
    }
    definitions=std::move(parsed);
}
ResolvedSound SoundDefinitions::resolve(const std::string &logical,uint32_t choice) const {
    ResolvedSound result;std::set<std::string> seen;std::string key=lower(logical);
    for(unsigned depth=0;depth<64;++depth) {
        if(!seen.insert(key).second)throw std::runtime_error("Cyclic SNDINFO sound: "+key);
        auto it=definitions.find(key);if(it==definitions.end())throw std::runtime_error("Undefined SNDINFO sound: "+key);
        const auto &d=it->second;result.volume*=d.volume;if(d.limit)result.limit=d.limit;
        if(d.alternatives.empty()){result.resource=d.resource;return result;}
        key=d.alternatives[choice%d.alternatives.size()];choice=choice*1664525u+1013904223u;
    }
    throw std::runtime_error("SNDINFO alias nesting too deep");
}
}
