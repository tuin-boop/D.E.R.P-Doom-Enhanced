/* Indirect light baked at level load: bounce, sky light and occlusion,
   and light flowing between sectors. */
#include "internal.h"

namespace doom3d {
int bounceKey=-1;

int bakeThingsKey=-1,ambientKey=-1,flowKey=-1;
float skyLevel=0; // Typical sky-sector light; indoor sky light rises toward it.
std::array<float,3> skyTint={1,1,1};
// Which inputs bounce light gathers: 1 always, 2 the sun, 4 baked lights,
// 8 bake-only lights (pools as areas).
int bounceInputs() {
    bool lights=lightBakeKey==lightBakeInputs();
    return 1|(settings.sun&&sunLevel>0?2:0)|(lights?4:0)|(lights&&(lightBakeKey&2)?8:0);
}
// Bounce light: each sample averages what 16 cosine-weighted rays hit,
// albedo (the surface texture's average color) times its sun and baked
// light. Samples are taken every 16 units and spread bilinearly over the
// cells between them within a sector or wall strip.
void bakeBounce() {
    loadingStage(LoadStage::Bounce);
    finishRelight();
    bounceKey=bounceInputs();
    if(flatLightCells.empty()||wallBakeCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    bool sun=bounceKey&2,lights=bounceKey&4;
    std::array<float,3> sunEnergy={};
    if(sun)for(int c=0;c<3;++c)sunEnergy[c]=sunTint[c]*sunLevel;
    std::vector<std::array<float,3>> flatAlbedo((size_t)numsectors*2,{0.5f,0.5f,0.5f});
    for(int i=0;i<numsectors;++i)for(int ceiling=0;ceiling<2;++ceiling) {
        int pic=ceiling?sectors[i].ceilingpic:sectors[i].floorpic;
        if(pic!=skyflatnum)flatAlbedo[(size_t)i*2+ceiling]=averageColor(lumpImage(firstflat+flattranslation[pic],true));
    }
    // Per side: middle, upper and lower texture.
    std::vector<std::array<float,3>> sideAlbedo((size_t)numlines*6,{0.5f,0.5f,0.5f});
    for(int i=0;i<numlines;++i)for(int side=0;side<2;++side) {
        if(lines[i].sidenum[side]<0)continue;
        const side_t &s=sides[lines[i].sidenum[side]];
        short textures[3]={s.midtexture,s.toptexture,s.bottomtexture};
        for(int part=0;part<3;++part)if(textures[part]>0)
            sideAlbedo[((size_t)i*2+side)*3+part]=averageColor(wallImage(texturetranslation[textures[part]]));
    }
    // Bounced light keeps only a quarter of the surfaces' own color: Doom's
    // texture averages are strongly tinted, and taken whole they scatter
    // patches of stray color around rooms. Lamp colors stay whole.
    for(auto *albedos:{&flatAlbedo,&sideAlbedo})for(auto &a:*albedos) {
        float grey=0.299f*a[0]+0.587f*a[1]+0.114f*a[2];
        for(float &c:a)c=grey+0.25f*(c-grey);
    }
    auto radiance=[&](const BakeHit &hit,float out[3]) {
        if(hit.kind==BakeHit::thing)return; // Decorations stay dark.
        const uint8_t *light=nullptr;float visible=0;const std::array<float,3> *albedo=nullptr;
        if(hit.kind==BakeHit::wall) {
            const BakeLine &line=bakeMap.lines[hit.line];
            int side=line.front==hit.sector?0:1;
            const BakeStrip &strip=bakeStrips[(size_t)hit.line*2+side];
            albedo=&sideAlbedo[((size_t)hit.line*2+side)*3+hit.part];
            if(strip.x<0)return;
            float along=(hit.x-strip.ax)*strip.dx+(hit.y-strip.ay)*strip.dy;
            int column=std::clamp((int)std::floor(along*strip.scale),0,strip.width-1);
            int row=std::clamp((int)std::floor((hit.z-strip.low)*strip.scale),0,strip.height-1);
            light=&wallBakeCells[4*((size_t)(strip.y+row)*atlasStride+strip.x+column)];
            visible=light[3]/255.0f;
        } else {
            int cx=std::clamp((int)std::floor((hit.x-mapOrigin[0])/mapCell),0,mapWidth-1);
            int cy=std::clamp((int)std::floor((hit.y-mapOrigin[1])/mapCell),0,mapHeight-1);
            size_t cell=(size_t)cy*mapWidth+cx;bool ceiling=hit.kind==BakeHit::ceiling;
            albedo=&flatAlbedo[(size_t)hit.sector*2+ceiling];
            light=flatCell(ceiling?ceilingLight:floorLight,cell);
            visible=ceiling?0:contactCells[4*cell+2]/255.0f;
        }
        // The surface shows its static light within the headroom its sector
        // leaves (fillHeadroom), as world.frag draws it.
        float shown[3]={};
        if(lights) {for(int c=0;c<3;++c)shown[c]=light[c]/255.0f*2;fillHeadroom(shown,lighting(&sectors[hit.sector]));}
        for(int c=0;c<3;++c)out[c]=(*albedo)[c]*(sunEnergy[c]*visible+shown[c]);
    };
    auto gather=[&](int sector,float x,float y,float z,const float normal[3],uint32_t seed,float out[3]) {
        // A per-sample rotation of the stratified pattern trades banding for fine noise.
        seed=seed*2654435761u;seed^=seed>>15;float rotation=(seed&1023)/1024.0f;
        out[0]=out[1]=out[2]=0;
        for(int n=0;n<16;++n) {
            float direction[3],hitLight[3]={};BakeHit hit;
            cosineDirection(normal,((n&3)+0.5f)/4,std::fmod(((n>>2)+0.5f)/4+rotation,1.0f),direction);
            if(traceRay(bakeMap,sector,x,y,z,direction,512,SkyCeiling::escape,&hit)||hit.kind==BakeHit::none)continue;
            radiance(hit,hitLight);
            for(int c=0;c<3;++c)out[c]+=hitLight[c]/16;
        }
    };
    // Unrounded (8 bits at half scale): bounce also colors the sector light
    // (lampTint), where 1/32 steps would band.
    auto encode=[](const float value[3],uint8_t *out) {
        for(int c=0;c<3;++c)out[c]=(uint8_t)std::min(255L,std::lround(value[c]*0.5f*255));
    };
    int step=std::max(1,(int)std::lround(16/mapCell));
    // Floors and ceilings: samples at block centers, then each cell blends the
    // samples around it that lie in its own sector; a cell with none nearby
    // (a sector smaller than a block) takes its own sample.
    int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
    std::vector<std::array<float,3>> samples((size_t)blocksX*blocksY*2);
    std::vector<int> sampleSector((size_t)blocksX*blocksY,noSector);
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    auto flatSample=[&](int sector,int x,int y,bool ceiling,float out[3]) {
        out[0]=out[1]=out[2]=0;
        int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
        if(pic==skyflatnum)return;
        const BakeSector &s=bakeMap.sectors[sector];
        gather(sector,mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,ceiling?s.ceiling-1:s.floor+1,
               ceiling?down:up,(uint32_t)(y*mapWidth+x)*2+ceiling,out);
    };
    parallelFor(blocksY,[&](int by) {
        for(int bx=0;bx<blocksX;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            size_t b=(size_t)by*blocksX+bx;int sector=seamCells[(size_t)y*mapWidth+x].own;
            sampleSector[b]=sector;if(sector==noSector)continue;
            for(int ceiling=0;ceiling<2;++ceiling)flatSample(sector,x,y,ceiling,samples[b*2+ceiling].data());
        }
    });
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            if(sector==noSector)continue;
            float gx=(x-step/2)/(float)step,gy=(y-step/2)/(float)step;
            int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
            float sum[2][3]={},total=0;
            for(int k=0;k<4;++k) {
                int bx=x0+(k&1),by=y0+(k>>1);
                if(bx<0||by<0||bx>=blocksX||by>=blocksY)continue;
                size_t b=(size_t)by*blocksX+bx;if(sampleSector[b]!=sector)continue;
                float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
                for(int ceiling=0;ceiling<2;++ceiling)for(int c=0;c<3;++c)sum[ceiling][c]+=samples[b*2+ceiling][c]*w;
                total+=w;
            }
            for(int ceiling=0;ceiling<2;++ceiling) {
                float value[3];
                if(total>0)for(int c=0;c<3;++c)value[c]=sum[ceiling][c]/total;
                else flatSample(sector,x,y,ceiling,value);
                encode(value,flatCell(ceiling?ceilingBounce:floorBounce,i));
            }
        }
    });
    // Walls: samples every step texels inside each strip, blended bilinearly.
    parallelFor((int)bakeOrder.size(),[&](int n) {
        const BakeStrip &strip=bakeStrips[bakeOrder[n]];
        const float normal[3]={strip.dy,-strip.dx,0};
        int columns=(strip.width+step-1)/step,rows=(strip.height+step-1)/step;
        std::vector<std::array<float,3>> coarse((size_t)columns*rows);
        for(int cy=0;cy<rows;++cy)for(int cx=0;cx<columns;++cx) {
            int column=std::min(cx*step+step/2,strip.width-1),row=std::min(cy*step+step/2,strip.height-1);
            float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
            float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
            gather(strip.sector,strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z,normal,
                   (uint32_t)bakeOrder[n]*7919u+cy*131u+cx,coarse[(size_t)cy*columns+cx].data());
        }
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            float gx=std::clamp((column-step/2)/(float)step,0.0f,columns-1.0f),gy=std::clamp((row-step/2)/(float)step,0.0f,rows-1.0f);
            int x0=std::min((int)gx,columns-1),y0=std::min((int)gy,rows-1),x1=std::min(x0+1,columns-1),y1=std::min(y0+1,rows-1);
            float fx=gx-x0,fy=gy-y0,value[3];
            for(int c=0;c<3;++c)
                value[c]=(coarse[(size_t)y0*columns+x0][c]*(1-fx)+coarse[(size_t)y0*columns+x1][c]*fx)*(1-fy)+
                         (coarse[(size_t)y1*columns+x0][c]*(1-fx)+coarse[(size_t)y1*columns+x1][c]*fx)*fy;
            encode(value,&wallBakeCells[4*((size_t)(strip.y+row)*atlasStride+atlasWidth+strip.x+column)]);
        }
    });
    uploadFlatLight();
    uploadWallBake();
    fprintf(stderr,"3D bounce: %s%s, every %d cells, baked in %.0f ms.\n",sun?"sun":"no sun",lights?" and lights":"",
        step,(SDL_GetTicksNS()-start)/1e6);
}
// Decorations in the bakes: static, non-glowing things that stand on the
// floor or hang from the ceiling (columns, trees, hanging bodies) occlude
// the sun, baked lights, bounce, sky and flow rays with their spawn-state
// sprite. Glowing ones are light sources and stay out, so a lamp never
// shadows its own light. Changing the setting re-bakes the level.
void syncBakeThings() {
    int key=settings.thingShadows;
    if(key==bakeThingsKey)return;
    invalidateStaticSamples();
    bool rebake=bakeThingsKey>=0;
    bakeThingsKey=key;
    bakeMap.things.clear();
    for(auto &sector:bakeMap.sectors)sector.things.clear();
    if(key)for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(thing->player||(thing->flags&(MF_COUNTKILL|MF_SPECIAL|MF_MISSILE|MF_NOBLOCKMAP|MF_SHOOTABLE|MF_CORPSE)))continue;
        if(!(thing->flags&(MF_SOLID|MF_SPAWNCEILING))||(thing->frame&FF_FULLBRIGHT))continue;
        if(thing->sprite<0||thing->sprite>=numsprites||(thing->frame&FF_FRAMEMASK)>=sprites[thing->sprite].numframes)continue;
        const Image &image=lumpImage(firstspritelump+sprites[thing->sprite].spriteframes[thing->frame&FF_FRAMEMASK].lump[0],false);
        float x=units(thing->x),y=units(thing->y),half=image.width*0.5f;
        int index=(int)bakeMap.things.size();
        bakeMap.things.push_back({x,y,units(thing->z)+image.top,-(float)image.left,image.width,image.height,image.pixels.data()});
        // Also list it in neighbors its sprite reaches across a line.
        std::vector<int> reach={i};
        for(int j=0;j<sectors[i].linecount;++j) {
            const line_t &line=*sectors[i].lines[j];
            const sector_t *other=line.frontsector==&sectors[i]?line.backsector:line.frontsector;
            if(!other)continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay,length2=ex*ex+ey*ey;
            if(length2<0.01f)continue;
            float t=std::clamp(((x-ax)*ex+(y-ay)*ey)/length2,0.0f,1.0f);
            int o=int(other-sectors);
            if(std::hypot(x-ax-ex*t,y-ay-ey*t)<half&&std::find(reach.begin(),reach.end(),o)==reach.end())reach.push_back(o);
        }
        for(int s:reach)bakeMap.sectors[s].things.push_back(index);
    }
    if(rebake) {sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;flowKey=-1;}
    if(key)fprintf(stderr,"3D bake: %zu decorations occlude baked light.\n",bakeMap.things.size());
}
// Spreads samples taken every step cells over the cells between them,
// bilinearly within one sector or wall strip; values are per sample channels.
// Floors and ceilings: sample(sector,x,y,ceiling,out) at block centers,
// write(cell,ceiling,value) per cell. Walls: sample(strip,x,y,z,out) and
// write(atlas texel,value).
template<int Channels,class FlatSample,class FlatWrite,class WallSample,class WallWrite>
static void bakeSpread(int step,FlatSample flatSample,FlatWrite flatWrite,WallSample wallSample,WallWrite wallWrite) {
    using Value=std::array<float,Channels>;
    int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
    std::vector<Value> samples((size_t)blocksX*blocksY*2);
    std::vector<int> sampleSector((size_t)blocksX*blocksY,noSector);
    parallelFor(blocksY,[&](int by) {
        for(int bx=0;bx<blocksX;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            size_t b=(size_t)by*blocksX+bx;int sector=seamCells[(size_t)y*mapWidth+x].own;
            sampleSector[b]=sector;if(sector==noSector)continue;
            for(int ceiling=0;ceiling<2;++ceiling)flatSample(sector,x,y,ceiling!=0,samples[b*2+ceiling]);
        }
    });
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            if(sector==noSector)continue;
            float gx=(x-step/2)/(float)step,gy=(y-step/2)/(float)step;
            int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
            Value sum[2]={};float total=0;
            for(int k=0;k<4;++k) {
                int bx=x0+(k&1),by=y0+(k>>1);
                if(bx<0||by<0||bx>=blocksX||by>=blocksY)continue;
                size_t b=(size_t)by*blocksX+bx;if(sampleSector[b]!=sector)continue;
                float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
                for(int ceiling=0;ceiling<2;++ceiling)for(int c=0;c<Channels;++c)sum[ceiling][c]+=samples[b*2+ceiling][c]*w;
                total+=w;
            }
            for(int ceiling=0;ceiling<2;++ceiling) {
                Value value;
                if(total>0)for(int c=0;c<Channels;++c)value[c]=sum[ceiling][c]/total;
                else flatSample(sector,x,y,ceiling!=0,value);
                flatWrite(i,ceiling!=0,value);
            }
        }
    });
    parallelFor((int)bakeOrder.size(),[&](int n) {
        const BakeStrip &strip=bakeStrips[bakeOrder[n]];
        int columns=(strip.width+step-1)/step,rows=(strip.height+step-1)/step;
        std::vector<Value> coarse((size_t)columns*rows);
        for(int cy=0;cy<rows;++cy)for(int cx=0;cx<columns;++cx) {
            int column=std::min(cx*step+step/2,strip.width-1),row=std::min(cy*step+step/2,strip.height-1);
            float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
            float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
            wallSample(strip,strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z,
                       (uint32_t)bakeOrder[n]*7919u+cy*131u+cx,coarse[(size_t)cy*columns+cx]);
        }
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            float gx=std::clamp((column-step/2)/(float)step,0.0f,columns-1.0f),gy=std::clamp((row-step/2)/(float)step,0.0f,rows-1.0f);
            int x0=std::min((int)gx,columns-1),y0=std::min((int)gy,rows-1),x1=std::min(x0+1,columns-1),y1=std::min(y0+1,rows-1);
            float fx=gx-x0,fy=gy-y0;Value value;
            for(int c=0;c<Channels;++c)
                value[c]=(coarse[(size_t)y0*columns+x0][c]*(1-fx)+coarse[(size_t)y0*columns+x1][c]*fx)*(1-fy)+
                         (coarse[(size_t)y1*columns+x0][c]*(1-fx)+coarse[(size_t)y1*columns+x1][c]*fx)*fy;
            wallWrite((size_t)(strip.y+row)*atlasStride+strip.x+column,value);
        }
    });
}
// Sky light and ambient occlusion, baked together from 12 cosine-weighted
// rays per sample, every ~8 units: the share that escapes through a sky
// ceiling (walls see at most half the sky, so theirs counts double) and how
// open the surroundings are within 64 units. Each is stored in 16 steps,
// like Doom's light levels, packed into the alpha of the bounce layers:
// sky in the high nibble, openness in the low one.
void bakeAmbient() {
    loadingStage(LoadStage::Ambient);
    finishRelight();
    ambientKey=bakeThingsKey;
    if(flatLightCells.empty()||wallBakeCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    std::vector<float> skyLights;
    for(int i=0;i<numsectors;++i)if(sectors[i].ceilingpic==skyflatnum)skyLights.push_back(lighting(&sectors[i]));
    skyLevel=0;
    if(!skyLights.empty()) {
        std::nth_element(skyLights.begin(),skyLights.begin()+skyLights.size()/2,skyLights.end());
        skyLevel=skyLights[skyLights.size()/2];
        skyTint=skyColor();
    }
    // Occlusion counts only hits within 64 units; rays run on only to find
    // the sky, so on maps without one they stop there, open either way.
    float reach=skyLights.empty()?64:INFINITY;
    auto gather=[&](int sector,float x,float y,float z,const float normal[3],uint32_t seed,std::array<float,2> &out) {
        seed=seed*2654435761u;seed^=seed>>15;float rotation=(seed&1023)/1024.0f;
        float sky=0,open=0;
        for(int n=0;n<12;++n) {
            float direction[3];BakeHit hit;
            cosineDirection(normal,((n&3)+0.5f)/4,std::fmod(((n>>2)+0.5f)/3+rotation,1.0f),direction);
            if(traceRay(bakeMap,sector,x,y,z,direction,reach,SkyCeiling::escape,&hit)) {sky+=reach==INFINITY;open+=1;continue;}
            if(hit.kind==BakeHit::none) {open+=1;continue;}
            open+=std::min(1.0f,std::sqrt((hit.x-x)*(hit.x-x)+(hit.y-y)*(hit.y-y)+(hit.z-z)*(hit.z-z))/64);
        }
        out={sky/12,open/12};
    };
    auto pack=[](float sky,float open) {
        return (uint8_t)((std::lround(std::clamp(sky,0.0f,1.0f)*15)<<4)|std::lround(std::clamp(open,0.0f,1.0f)*15));
    };
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    bakeSpread<2>(std::max(1,(int)std::lround(8/mapCell)),
        [&](int sector,int x,int y,bool ceiling,std::array<float,2> &out) {
            out={0,1};
            int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
            if(pic==skyflatnum)return;
            const BakeSector &s=bakeMap.sectors[sector];
            gather(sector,mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,ceiling?s.ceiling-1:s.floor+1,
                   ceiling?down:up,(uint32_t)(y*mapWidth+x)*2+ceiling,out);
        },
        [&](size_t cell,bool ceiling,const std::array<float,2> &value) {flatCell(ceiling?ceilingBounce:floorBounce,cell)[3]=pack(value[0],value[1]);},
        [&](const BakeStrip &strip,float x,float y,float z,uint32_t seed,std::array<float,2> &out) {
            const float normal[3]={strip.dy,-strip.dx,0};
            gather(strip.sector,x,y,z,normal,seed,out);out[0]*=2;
        },
        [&](size_t texel,const std::array<float,2> &value) {wallBakeCells[4*(texel+atlasWidth)+3]=pack(value[0],value[1]);});
    uploadFlatLight();
    uploadWallBake();
    fprintf(stderr,"3D sky light and occlusion: sky level %.2f, baked in %.0f ms.\n",skyLevel,(SDL_GetTicksNS()-start)/1e6);
}
// Sector light flow: from points every ~16 units, at chest height, 12 level
// rays check which other sectors are in plain view at 48, 96 and 160 units.
// The most visible one is stored per cell as its flow neighbor (seam map,
// fourth channel) with a weight (contact map alpha); during play the cell's
// light rises toward that sector's current light, so flickering rooms
// flicker faintly beyond their openings. Doors count as they start (closed).
void bakeFlow() {
    loadingStage(LoadStage::Flow);
    finishRelight();
    flowKey=flowInputs();
    bool windows=flowKey&2;
    if(seamCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    for(auto &cell:seamCells)cell.flow=noSector;
    struct Flow { int neighbor=noSector;float weight=0; };
    int step=std::max(1,(int)std::lround(16/mapCell));
    int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
    std::vector<Flow> blocks((size_t)blocksX*blocksY);
    std::vector<int> blockSector((size_t)blocksX*blocksY,noSector);
    parallelFor(blocksY,[&](int by) {
        std::vector<std::pair<int,float>> score;
        for(int bx=0;bx<blocksX;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            size_t b=(size_t)by*blocksX+bx;int sector=seamCells[(size_t)y*mapWidth+x].own;
            blockSector[b]=sector;if(sector==noSector)continue;
            const BakeSector &s=bakeMap.sectors[sector];
            if(s.ceiling-s.floor<8)continue;
            float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell,pz=s.floor+std::min(40.0f,(s.ceiling-s.floor)*0.5f);
            float rotation=hashUnit(bx,by,0x51f1u);
            score.clear();
            for(int k=0;k<12;++k) {
                float angle=(k+rotation)*2*doomPi/12,direction[3]={std::cos(angle),std::sin(angle),0};
                static const std::pair<float,float> lengths[]={{48,1.0f},{96,0.6f},{160,0.35f}};
                for(auto [length,weight]:lengths) {
                    if(!traceRay(bakeMap,sector,px,py,pz,direction,length,SkyCeiling::open))break;
                    float ex=px+direction[0]*length,ey=py+direction[1]*length;
                    int end=int(R_PointInSubsector((fixed_t)(ex*FRACUNIT),(fixed_t)(ey*FRACUNIT))->sector-sectors);
                    if(end==sector)continue;
                    if(windows&&sectors[end].ceilingpic==skyflatnum&&sectors[sector].ceilingpic!=skyflatnum)continue;
                    auto found=std::find_if(score.begin(),score.end(),[&](const auto &e){return e.first==end;});
                    if(found==score.end())score.push_back({end,weight});else found->second+=weight;
                }
            }
            for(const auto &entry:score)if(entry.second>blocks[b].weight)blocks[b]={entry.first,entry.second};
            blocks[b].weight=std::min(1.0f,blocks[b].weight*2/(12*1.95f));
        }
    });
    // Cells blend the weights of nearby same-sector blocks that share the
    // strongest one's neighbor.
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            contactCells[4*i+3]=0;
            if(sector==noSector)continue;
            float gx=(x-step/2)/(float)step,gy=(y-step/2)/(float)step;
            int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
            int neighbor=noSector;float strongest=0,sum=0,total=0;
            for(int pass=0;pass<2;++pass)for(int k=0;k<4;++k) {
                int bx=x0+(k&1),by=y0+(k>>1);
                if(bx<0||by<0||bx>=blocksX||by>=blocksY)continue;
                size_t b=(size_t)by*blocksX+bx;if(blockSector[b]!=sector)continue;
                const Flow &flow=blocks[b];
                if(pass==0) {if(flow.weight>strongest){strongest=flow.weight;neighbor=flow.neighbor;}continue;}
                float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
                sum+=flow.neighbor==neighbor?flow.weight*w:0;total+=w;
            }
            if(neighbor==noSector||total<=0)continue;
            seamCells[i].flow=(uint16_t)neighbor;
            contactCells[4*i+3]=(uint8_t)std::lround(std::clamp(sum/total,0.0f,1.0f)*255);
        }
    });
    seamTexture=gpuCreateTexture(GpuFormat::RGBA16Uint,mapWidth,mapHeight,seamCells.data(),mapWidth*8);
    contactTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,contactCells.data(),mapWidth*4);
    if(!seamTexture||!contactTexture)I_Error((char*)"Could not allocate surface maps");
    fprintf(stderr,"3D light flow: every %d cells, baked in %.0f ms.\n",step,(SDL_GetTicksNS()-start)/1e6);
}
// CPU twin of flowLight in world.frag, for sprites and the weapon.
float flowLightAt(float x,float y,const sector_t *sector,float light) {
    if(!settings.lightFlow||flowKey<0||seamCells.empty())return light;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return light;
    size_t i=(size_t)cy*mapWidth+cx;const SeamCell &cell=seamCells[i];
    if(cell.own!=(uint16_t)(sector-sectors)||cell.flow==noSector)return light;
    return light+std::round(std::max(0.0f,lighting(&sectors[cell.flow])-light)*contactCells[4*i+3]/255.0f*0.8f*16)/16;
}
void indirectNewLevel() {bounceKey=-1;ambientKey=-1;flowKey=-1;bakeThingsKey=-1;}
void indirectShutdown() {indirectNewLevel();skyLevel=0;}
} // namespace doom3d
