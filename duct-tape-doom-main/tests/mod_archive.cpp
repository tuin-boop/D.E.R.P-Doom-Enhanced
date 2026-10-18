#include "../platform/mod_archive.h"
#include "miniz.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
int main(int argc,char **argv) {
    try {
        if(argc>1) {
            for(int i=1;i<argc;++i) {
                doommod::Archive archive(argv[i]);size_t count=0,bytes=0;
                for(const auto &name:archive.names()) {auto data=archive.read(name);bytes+=data.size();++count;}
                CHECK(archive.contains("SNDINFO.TXT"));
                std::printf("Validated %zu resources (%zu bytes, all CRCs checked): %s\n",count,bytes,argv[i]);
            }
            return 0;
        }
        const char *path="mod-archive-test.pk3";mz_zip_archive zip{};
        CHECK(mz_zip_writer_init_file(&zip,path,0));
        CHECK(mz_zip_writer_add_mem(&zip,"MRSmooth-Code/Weapons/Pistol.zsc","old",3,0));
        CHECK(mz_zip_writer_add_mem(&zip,"mrsmooth-code/weapons/PISTOL.zsc","replacement",11,MZ_BEST_COMPRESSION));
        CHECK(mz_zip_writer_add_mem(&zip,"sounds/empty.wav","",0,0));
        CHECK(mz_zip_writer_finalize_archive(&zip));CHECK(mz_zip_writer_end(&zip));
        { doommod::Archive a(path);
          CHECK(a.text("MRSMOOTH-code/WEAPONS/pistol.ZSC")=="replacement");
          CHECK(a.read("sounds/empty.wav").empty());CHECK(a.names().size()==2);
          bool missing=false;try{a.read("absent");}catch(const std::runtime_error &){missing=true;}CHECK(missing);
        }
        std::remove(path);
        for(const char *bad:{"../secret","/absolute","x/../secret","x//y","x/./y","C:/secret","a\\b","x/"}) {
            bool rejected=false;try{doommod::Archive::normalize(bad);}catch(const std::runtime_error &){rejected=true;}CHECK(rejected);
        }
        puts("Full-path PK3 access: namespace, case, duplicate precedence, empty resources and unsafe-path checks passed");return 0;
    }catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}
}
