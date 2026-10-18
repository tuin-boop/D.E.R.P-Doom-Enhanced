/* Bounded UMAPINFO metadata reader. Unknown properties are ignored; no script
 * code is evaluated. Later loaded entries replace earlier map metadata. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "doomstat.h"
#include "w_wad.h"
#include "z_zone.h"
#include "g_mapinfo.h"
static mapinfo_t maps[180];
static int count;
static int equal(const char *a,const char *b){while(*a&&*b){if(toupper((unsigned char)*a++)!=toupper((unsigned char)*b++))return 0;}return !*a&&!*b;}
static void copy(char *to,size_t size,const char *from){snprintf(to,size,"%s",from);}
static int token(const char **cursor,const char *end,char *out,size_t size){
    const char *p=*cursor;size_t n=0;int quoted=0;
    for(;;){
        while(p<end&&isspace((unsigned char)*p))++p;
        if(p+1<end&&p[0]=='/'&&p[1]=='/'){while(p<end&&*p!='\n')++p;continue;}
        if(p+1<end&&p[0]=='/'&&p[1]=='*'){p+=2;while(p+1<end&&!(p[0]=='*'&&p[1]=='/'))++p;if(p+1<end)p+=2;else p=end;continue;}
        break;
    }
    if(p>=end){*cursor=end;out[0]=0;return 0;}
    if(*p=='"'){quoted=1;++p;while(p<end&&*p!='"'){char c=*p++;if(c=='\\'&&p<end){c=*p++;if(c=='n')c='\n';}if(n+1<size)out[n++]=c;}if(p<end)++p;}
    else if(strchr("{}=,",*p)){out[n++]=*p++;}
    else while(p<end&&!isspace((unsigned char)*p)&&!strchr("{}=,",*p)){if(n+1<size)out[n++]=*p;++p;}
    out[n]=0;*cursor=p;return quoted?2:1;
}
static void parse(const char *data,size_t size){
    const char *p=data,*end=data+size;char word[2048],key[64],value[2048];
    while(token(&p,end,word,sizeof(word))){
        if(!equal(word,"map"))continue;
        if(!token(&p,end,word,sizeof(word))||strlen(word)>8)continue;
        mapinfo_t parsed={0};copy(parsed.map,sizeof(parsed.map),word);
        if(!token(&p,end,word,sizeof(word))||strcmp(word,"{"))continue;
        while(token(&p,end,key,sizeof(key))&&strcmp(key,"}")){
            if(!token(&p,end,word,sizeof(word))||strcmp(word,"="))continue;
            if(!token(&p,end,value,sizeof(value)))break;
            char *field=NULL;size_t cap=0;
#define FIELD(name,member) if(equal(key,name)){field=parsed.member;cap=sizeof(parsed.member);}
            FIELD("levelname",title) else FIELD("skytexture",sky) else FIELD("music",music)
            else FIELD("levelpic",levelpic) else FIELD("exitpic",exitpic) else FIELD("endpic",endpic)
            else FIELD("next",next) else FIELD("nextsecret",secret) else FIELD("intertext",intertext)
            else FIELD("episode",episodepic)
#undef FIELD
            if(field)copy(field,cap,value);
            int item=0;
            for(;;){const char *saved=p;if(!token(&p,end,word,sizeof(word))||strcmp(word,",")){p=saved;break;}
                if(!token(&p,end,value,sizeof(value)))break;++item;
                if(equal(key,"intertext")){size_t len=strlen(parsed.intertext);if(len+1<sizeof(parsed.intertext)){parsed.intertext[len++]='\n';copy(parsed.intertext+len,sizeof(parsed.intertext)-len,value);}}
                if(equal(key,"episode")&&item==1)copy(parsed.episodename,sizeof(parsed.episodename),value);
            }
        }
        int n=0;while(n<count&&!equal(maps[n].map,parsed.map))++n;
        if(n<180){maps[n]=parsed;if(n==count)++count;}
    }
}
void G_LoadMapInfo(void){
    count=0;
    for(int i=0;i<numlumps;++i)if(!memcmp(lumpinfo[i].name,"UMAPINFO",8)){
        int size=W_LumpLength(i);if(size<=0||size>1024*1024)continue;
        const char *data=W_CacheLumpNum(i,PU_CACHE);parse(data,(size_t)size);
    }
}
const mapinfo_t *G_MapInfo(int episode,int map){
    char name[16];if(gamemode==commercial)snprintf(name,sizeof(name),"MAP%02d",map);else snprintf(name,sizeof(name),"E%dM%d",episode,map);
    for(int n=0;n<count;++n)if(equal(name,maps[n].map))return &maps[n];
    return NULL;
}
