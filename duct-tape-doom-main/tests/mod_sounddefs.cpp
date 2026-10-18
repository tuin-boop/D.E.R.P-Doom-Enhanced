#include "../platform/mod_sounddefs.h"
#include "../platform/mod_archive.h"
#include <cstdio>
#include <cmath>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
int main(int argc,char **argv) {
    try {
        doommod::SoundDefinitions defs;
        if(argc>1) {
            for(int i=1;i<argc;++i){doommod::Archive a(argv[i]);defs.parse(a.text("sndinfo.txt"));}
            CHECK(defs.all().size()>100);
            auto sound=defs.resolve("MRIntW/HQ/PistolFire");CHECK(sound.resource.find("MRIntHQPistol.ogg")!=std::string::npos);
            sound=defs.resolve("MRIntW/HQ/PistolSelect",3);CHECK(std::abs(sound.volume-.4f)<.0001f);
            printf("Parsed %zu logical sounds from the actual Fluid Weapons packages\n",defs.all().size());return 0;
        }
        defs.parse("// comment\nFire \"sounds/pistol.ogg\"\nSilent \"\"\nAlt dsalt\n$random Pick { Fire Alt }\n$volume Pick .4\n$limit Fire 2\n$alias Alias Pick\n");
        CHECK(defs.resolve("FIRE").resource=="sounds/pistol.ogg");CHECK(defs.resolve("Silent").resource.empty());
        CHECK(defs.resolve("Pick",0).limit==2);CHECK(defs.resolve("Pick",1).resource=="dsalt");
        CHECK(std::abs(defs.resolve("Alias").volume-.4f)<.0001f);
        defs.parse("Fire \"sounds/new.ogg\" /* retained volume/limit */");CHECK(defs.resolve("fire").resource=="sounds/new.ogg");
        bool caught=false;try{defs.parse("Fire bad\n$unknown x\n");}catch(const std::runtime_error &){caught=true;}CHECK(caught);CHECK(defs.resolve("Fire").resource=="sounds/new.ogg");
        for(const char *bad:{"$random empty {}","$random x { y","$volume x nan","$limit x -1","x \"unterminated","/* no close"}) {
            caught=false;try{defs.parse(bad);}catch(const std::runtime_error &){caught=true;}CHECK(caught);
        }
        defs.parse("$alias a b\n$alias b a");caught=false;try{defs.resolve("a");}catch(const std::runtime_error &){caught=true;}CHECK(caught);
        puts("SNDINFO mappings, random selection, aliases, volume, limits, load order and invalid-input checks passed");return 0;
    }catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}
}
