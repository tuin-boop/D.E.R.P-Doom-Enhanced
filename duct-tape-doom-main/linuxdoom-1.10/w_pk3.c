/* Native PK3 container support. ZIP entries are never extracted by pathname.
 * Classic lumps and embedded WADs are indexed in a private temporary stream.
 * This is not an implementation of the ZDoom scripting/rendering APIs. */
#include "i_compat.h"
#include "i_system.h"
#include "w_wad.h"
#include "miniz.h"
#include <ctype.h>
#include <stdint.h>

#define PK3_LIMIT (512u * 1024u * 1024u)
#define ENTRY_LIMIT (64u * 1024u * 1024u)
static FILE *streams[20];
static int stream_count;
static void cleanup(void) { while (stream_count) fclose(streams[--stream_count]); }
static uint32_t le32(const unsigned char *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void add_lump(FILE *f, const char *name, int pos, int size)
{
    if (numlumps >= 1000000) I_Error("PK3: too many lumps");
    lumpinfo_t *grown = realloc(lumpinfo, (size_t)(numlumps+1)*sizeof(*lumpinfo));
    if (!grown) I_Error("PK3: out of memory");
    lumpinfo = grown;
    lumpinfo_t *l = &lumpinfo[numlumps++];
    memset(l, 0, sizeof(*l));
    for (int i=0; i<8 && name[i]; i++) l->name[i]=(char)toupper((unsigned char)name[i]);
    l->handle=fileno(f); l->position=pos; l->size=size;
}
static int starts(const char *s, const char *prefix)
{ return !strncasecmp(s,prefix,strlen(prefix)); }
static int forbidden(const char *name)
{
    const char *b=strrchr(name,'/'); b=b ? b+1 : name;
    char stem[256]; size_t n=strcspn(b,".");
    if(n>=sizeof(stem)) return 1;
    memcpy(stem,b,n); stem[n]=0;
    const char *scripts[]={"ZSCRIPT","DECORATE","GLDEFS","MAPINFO","ZMAPINFO","VOXELDEF","MODELDEF","TEXTURES","SNDINFO","SNDSEQ","ANIMDEFS","SBARINFO","MENUDEF","CVARINFO","LOADACS","GAMEINFO",NULL};
    for(int i=0;scripts[i];i++) if(!strcasecmp(stem,scripts[i])) return 1;
    return starts(name,"filter/") || starts(name,"shaders/") || starts(name,"voxels/") || starts(name,"models/") || starts(name,"acs/");
}
void W_AddPK3(const char *filename)
{
    mz_zip_archive z; memset(&z,0,sizeof(z));
    if(!mz_zip_reader_init_file(&z,filename,0)) I_Error("Cannot read PK3: %s",filename);
    mz_uint count=mz_zip_reader_get_num_files(&z);
    if(count>100000) I_Error("PK3 has too many entries");
    uint64_t total=0;
    /* Preflight every entry before loading any data. */
    for(mz_uint i=0;i<count;i++) {
        mz_zip_archive_file_stat st;
        if(!mz_zip_reader_file_stat(&z,i,&st)) I_Error("Invalid PK3 directory");
        const char *name=st.m_filename;
        if(strlen(name)>=sizeof(st.m_filename)-1 || name[0]=='/' || strchr(name,'\\') || strchr(name,':') || strstr(name,"../")) I_Error("Unsafe or oversized PK3 entry name");
        if(st.m_is_directory) continue;
        if(forbidden(name)) I_Error("PK3 needs unsupported ZDoom features: %s\nDuct Tape Doom cannot execute ZScript, DECORATE, shaders or voxels.",name);
        total+=st.m_uncomp_size;
        if(st.m_uncomp_size>ENTRY_LIMIT || total>PK3_LIMIT || st.m_is_encrypted || !mz_zip_reader_is_file_supported(&z,i)) I_Error("PK3 entry is encrypted, unsupported or exceeds size limits");
    }
    if(stream_count>=20) I_Error("Too many PK3 archives");
    FILE *f=tmpfile(); if(!f) I_Error("Cannot create PK3 temporary stream");
    if(!stream_count) atexit(cleanup);
    streams[stream_count++]=f;
    int before=numlumps;
    /* Archive order is retained, including embedded WAD ordering. */
    for(mz_uint i=0;i<count;i++) {
        mz_zip_archive_file_stat st; mz_zip_reader_file_stat(&z,i,&st);
        if(st.m_is_directory) continue;
        const char *name=st.m_filename;
        const char *b=strrchr(name,'/'); b=b?b+1:name;
        const char *ext=strrchr(b,'.');
        if(ext && (!strcasecmp(ext,".txt") || !strcasecmp(ext,".md"))) continue;
        size_t len=0; unsigned char *data=mz_zip_reader_extract_to_heap(&z,i,&len,0);
        if(!data) I_Error("PK3 decompression or CRC check failed: %s",name);
        long offset=ftell(f);
        if(offset<0 || (uint64_t)offset+len>PK3_LIMIT || fwrite(data,1,len,f)!=len) I_Error("PK3 temporary storage failed");
        if(ext && !strcasecmp(ext,".wad")) {
            if(len<12 || (memcmp(data,"PWAD",4) && memcmp(data,"IWAD",4))) I_Error("Invalid embedded WAD: %s",name);
            uint32_t n=le32(data+4), dir=le32(data+8);
            if(!n || n>100000 || dir<12 || (uint64_t)dir+(uint64_t)n*16>len) I_Error("Invalid embedded WAD directory");
            for(uint32_t j=0;j<n;j++) {
                const unsigned char *e=data+dir+j*16;
                uint32_t pos=le32(e), size=le32(e+4);
                if((uint64_t)pos+size>len) I_Error("Embedded WAD lump outside file");
                char lumpname[9]; memcpy(lumpname,e+8,8); lumpname[8]=0;
                if(forbidden(lumpname)) I_Error("Embedded WAD needs unsupported ZDoom feature: %s",lumpname);
                add_lump(f,lumpname,(int)offset+(int)pos,(int)size);
            }
        } else {
            size_t n=ext?(size_t)(ext-b):strlen(b);
            if(n<1 || n>8) I_Error("PK3 classic lump names must have 1-8 characters: %s",name);
            if(len>=8 && !memcmp(data,"\211PNG\r\n\032\n",8)) I_Error("PK3 PNG graphics are not supported; use Doom-format graphics: %s",name);
            if(strchr(name,'/') && !(starts(name,"flats/") || starts(name,"patches/") || starts(name,"graphics/") || starts(name,"sounds/") || starts(name,"music/"))) I_Error("Unsupported PK3 folder: %s",name);
            char lumpname[9]; memcpy(lumpname,b,n); lumpname[n]=0;
            int flat=starts(name,"flats/");
            if(flat && len!=4096) I_Error("PK3 flat must be 64x64 indexed pixels: %s",name);
            if(flat) add_lump(f,"F_START",0,0);
            add_lump(f,lumpname,(int)offset,(int)len);
            if(flat) add_lump(f,"F_END",0,0);
        }
        mz_free(data);
    }
    if(fflush(f)) I_Error("PK3 temporary stream flush failed");
    mz_zip_reader_end(&z);
    if(numlumps==before) I_Error("PK3 contains no compatible game data");
}
