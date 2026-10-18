#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "miniz.h"
#include "w_wad.h"
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"Failed: %s\n",#x); return 2;}}while(0)
int main(int argc,char **argv) {
    const char *mode=argc>1?argv[1]:"valid";
    char path[128]; snprintf(path,sizeof(path),"pk3-%s.pk3",mode);
    mz_zip_archive zip;memset(&zip,0,sizeof(zip));
    CHECK(mz_zip_writer_init_file(&zip,path,0));
    unsigned char wad[32]={'P','W','A','D',1,0,0,0,16,0,0,0,'t','e','s','t',12,0,0,0,4,0,0,0,'H','E','L','L','O',0,0,0};
    if(!strcmp(mode,"badwad")) wad[16]=255;
    CHECK(mz_zip_writer_add_mem(&zip,"maps/test.wad",wad,sizeof(wad),MZ_BEST_COMPRESSION));
    CHECK(mz_zip_writer_add_mem(&zip,"graphics/hello.lmp","last",4,0));
    unsigned char flat[4096];memset(flat,42,sizeof(flat));
    CHECK(mz_zip_writer_add_mem(&zip,"flats/TESTFLAT.lmp",flat,sizeof(flat),MZ_BEST_COMPRESSION));
    if(!strcmp(mode,"script")) CHECK(mz_zip_writer_add_mem(&zip,"zscript.txt","class test {}",13,0));
    if(!strcmp(mode,"traversal")) CHECK(mz_zip_writer_add_mem(&zip,"../escape.lmp","bad",3,0));
    if(!strcmp(mode,"png")) CHECK(mz_zip_writer_add_mem(&zip,"graphics/PNG.png","\211PNG\r\n\032\n",8,0));
    CHECK(mz_zip_writer_finalize_archive(&zip));CHECK(mz_zip_writer_end(&zip));
    char *files[]={path,NULL};W_InitMultipleFiles(files);
    CHECK(!strcmp(mode,"valid"));
    char output[4096]; int lump=W_GetNumForName("HELLO"); CHECK(W_LumpLength(lump)==4);
    W_ReadLump(lump,output); CHECK(!memcmp(output,"last",4));
    lump=W_GetNumForName("TESTFLAT"); CHECK(W_LumpLength(lump)==4096);W_ReadLump(lump,output);CHECK(output[0]==42 && output[4095]==42);
    CHECK(W_CheckNumForName("F_START")>=0 && W_CheckNumForName("F_END")>=0);
    remove(path);puts("Native PK3: compressed embedded WAD, stored override and flat checks passed");return 0;
}
