#include "../platform/baked_lighting.h"
#include "../platform/shadow_lights.h"
#include <cstdio>
#include <cstdlib>

void check(bool condition,const char *message) {
    if(!condition) { std::fprintf(stderr,"%s\n",message);std::exit(1); }
}
// Three 128-unit rooms in a row along x: 0 sky courtyard, 1 middle, 2 far.
BakeMap rooms(BakeSector courtyard,BakeSector middle,BakeSector far) {
    BakeMap map;
    map.sectors={courtyard,middle,far};
    auto add=[&](float ax,float ay,float bx,float by,int front,int back) {
        int index=(int)map.lines.size();map.lines.push_back({ax,ay,bx,by,front,back});
        for(int s:{front,back})if(s>=0)map.sectors[s].lines.push_back(index);
    };
    for(int room=0;room<3;++room) {
        float x=room*128.0f;
        add(x,0,x+128,0,room,-1);add(x+128,128,x,128,room,-1);
    }
    add(0,128,0,0,0,-1);add(128,0,128,128,0,1);add(256,0,256,128,1,2);add(384,0,384,128,2,-1);
    return map;
}
float baked(const BakeMap &map,const BakeLight &light,int sector,float x,float z,const float normal[3]) {
    float out[3]={};addBakedLight(map,light,sector,x,64,z,normal,out);return out[0];
}
int main() {
    // A large concave room exercises tree pruning, axis-aligned rays and ties.
    BakeMap reference;reference.sectors.push_back({0,128,false,{}});
    for(int i=0;i<160;++i) {
        float a=i*6.28318530718f/160,b=(i+1)*6.28318530718f/160;
        float ra=i%2?220.0f:300.0f,rb=(i+1)%2?220.0f:300.0f;
        reference.lines.push_back({ra*std::cos(a),ra*std::sin(a),rb*std::cos(b),rb*std::sin(b),0,-1});
        reference.sectors[0].lines.push_back(i);
    }
    BakeMap accelerated=reference;buildBakeLineTrees(accelerated);
    for(int i=0;i<12000;++i) {
        float a=i*0.037f,d[3]={std::cos(a),std::sin(a),(i%17-8)*0.04f};
        if(i%29==0)d[0]=0;if(i%31==0)d[1]=0;
        float x=(i%71)-35.0f,y=(i%43)-21.0f,length=i%3?INFINITY:120;
        BakeHit slow,fast;
        bool one=traceRay(reference,0,x,y,64,d,length,SkyCeiling::escape,&slow);
        bool two=traceRay(accelerated,0,x,y,64,d,length,SkyCeiling::escape,&fast);
        check(one==two&&slow.kind==fast.kind&&slow.line==fast.line&&slow.sector==fast.sector&&slow.x==fast.x&&slow.y==fast.y&&slow.z==fast.z,
              "Accelerated bake rays must preserve exact hits");
    }
    for(int tick=0;tick<100;++tick){
        doom3d::ShadowLights shadows;
        shadows.add({-40,0,80,tick%2?.6f:.4f});
        shadows.add({40,0,80,tick%2?.4f:.6f});
        shadows.add({-40,0,80,.3f}); // Duplicate baked representation.
        int count=0;for(const auto&light:shadows.lights)if(light.strength>0)++count;
        check(count==2,"Two flickering torches must keep two shadows without duplicating baked lights");
    }
    doom3d::ShadowLights crowded;
    for(int n=0;n<32;++n)crowded.add({float(n*10),0,80,float(n+1)});
    for(const auto&light:crowded.lights)check(light.strength>=25,"Shadow budget must retain the strongest eight lights");
    // 40 degrees of elevation, and a steeper 60 for the small courtyard.
    const float up[3]={0,0,1},east[3]={0.766f,0,0.643f},west[3]={-0.766f,0,0.643f};
    const float steepEast[3]={0.5f,0,0.866f},steepWest[3]={-0.5f,0,0.866f};
    BakeMap map=rooms({0,128,true,{}},{0,128,true,{}},{0,64,false,{}});
    check(sunReachesSky(map,0,64,64,1,up),"Open courtyard must see the sun");
    check(!sunReachesSky(map,2,320,64,1,up),"Indoor floor must be shaded");
    check(sunReachesSky(map,1,200,64,1,west),"Low sun must pass between sky sectors");
    check(!sunReachesSky(map,1,140,64,1,east),"Facade above a low indoor ceiling must cast a shadow");
    check(sunReachesSky(map,2,260,64,1,west),"Sun must enter a room through its opening");
    check(!sunReachesSky(map,2,380,64,1,west),"Low sun must not reach deep under a ceiling");
    check(sunVisibility(map,1,250,64,1,west)==1,"Rays away from any occluder must all reach the sun");
    map=rooms({0,128,true,{}},{64,128,true,{}},{0,64,false,{}});
    check(!sunReachesSky(map,0,120,64,1,steepEast),"A raised platform must cast a shadow");
    check(sunReachesSky(map,0,120,64,1,steepWest),"Shadow must fall on the side away from the sun");
    map=rooms({0,128,true,{}},{0,128,true,{}},{0,0,false,{}});
    check(!sunReachesSky(map,1,250,64,1,east),"A closed door must block the sun");

    const float floorUp[3]={0,0,1};
    BakeLight torch={64,64,48,320,1,0.75f,{1,0.5f,0.2f}};
    map=rooms({0,128,true,{}},{0,128,true,{}},{0,64,false,{}});
    float near=baked(map,torch,0,80,1,floorUp),far=baked(map,torch,1,200,1,floorUp);
    check(near>far&&far>0,"Baked light must fall off with distance across an opening");
    check(baked(map,torch,0,400,1,floorUp)==0,"Baked light must end at its radius");
    float dim[3]={0.3f,0.3f,0.3f},lamp[3]={2.5f,2.5f,2.5f},bright[3]={2.5f,2.5f,2.5f};
    fillHeadroom(dim,0.25f);fillHeadroom(lamp,0.5f);fillHeadroom(bright,224/255.0f);
    check(dim[0]==0.3f&&lamp[0]+0.5f<=bakeLimit&&lamp[0]>0.6f&&bright[0]+224/255.0f<=bakeLimit,
          "Static light must fill only the headroom a sector's light level leaves");
    {
        // A glowing panel (18..78 high) seen through a low opening that hides
        // its middle but not its bottom still lends part of its light.
        BakeLight panel={60,64,48,400,1,0.35f,{1,1,1}};panel.extent=30;
        BakeMap open=rooms({0,128,false,{}},{0,128,false,{}},{0,128,false,{}});
        BakeMap lip=rooms({0,128,false,{}},{0,30,false,{}},{0,128,false,{}});
        float full=baked(open,panel,2,300,1,floorUp),part=baked(lip,panel,2,300,1,floorUp);
        BakeLight point=panel;point.extent=0;
        check(part>0&&part<full&&baked(lip,point,2,300,1,floorUp)==0,
              "A panel's light must pass a lip that hides only part of it");
    }
    float red[3]={0.7f,0.077f,0.077f};
    fillHeadroom(red,1.0f);
    check(red[0]>0.5f&&0.299f*red[0]+0.587f*red[1]+0.114f*red[2]<=bakeHeadroom(1.0f),
          "A colored lamp must still tint a bright sector within its headroom");
    {
        float normalCoverage[3]={.008f,.008f,.008f},denseRed[3]={.236f,.047f,.014f};
        float redSource[3]={1,.04f,.04f},blueSource[3]={.04f,.04f,1};
        check(emissionCoverageGain(normalCoverage,redSource,false)==1,"Sparse fixtures must retain their original emission");
        check(emissionCoverageGain(denseRed,redSource,false)<.25f,"Widespread red emission must be attenuated automatically");
        check(emissionCoverageGain(denseRed,blueSource,false)==1,"A red-dominated map must preserve blue fixtures");
        check(emissionCoverageGain(denseRed,redSource,true)>=.4f,"Lava must retain useful nearby illumination");
        float normal[3]={0.5f,0.2f,0.1f},over[3]={4,0.4f,0.2f};
        check(bakeOverloadScale(normal)==1,"Ordinary colored lights must remain unchanged");
        float scale=bakeOverloadScale(over);
        check(scale<1 && over[0]*scale<1.4f && over[0]*scale>1,
              "Overlapping colored light must roll off below channel clipping");
        float previous=0;
        for(int i=0;i<1000;++i){float rgb[3]={i*.01f,0,0};float result=rgb[0]*bakeOverloadScale(rgb);
            check(result+1e-6f>=previous,"Adaptive light rolloff must be monotonic");previous=result;}
        float white[3]={4,4,4},zero[3]={0,0,0},tint[3];
        lampTint(white,zero,0,.5f,tint);
        check(tint[0]==1&&tint[1]==1&&tint[2]==1,"White lighting must stay neutral");
    }
    map=rooms({0,128,true,{}},{0,64,true,{}},{0,64,false,{}});
    torch.z=100;
    check(baked(map,torch,1,200,63,floorUp)>0,"Light must pass over a lower sky neighbor's ceiling");
    torch.z=48;
    map=rooms({0,128,true,{}},{0,0,false,{}},{0,64,false,{}});
    torch.x=320;
    check(baked(map,torch,0,100,1,floorUp)==0,"A closed door must block a baked light");

    // A grate in the opening between the courtyard and the middle room:
    // two-texel columns alternate open and solid from height 0 to 64.
    map=rooms({0,128,true,{}},{0,128,true,{}},{0,64,false,{}});
    unsigned char grate[4*1*2]={0,0,0,0,0,255,0,255};
    for(auto &line:map.lines)if(line.front==0&&line.back==1)line.mask={grate,4,1,0,1,64,0,64};
    const float flat[3]={1,0,0};
    check(traceRay(map,0,64,1,32,flat,128,SkyCeiling::open),"Rays must pass a grate's open texels");
    check(!traceRay(map,0,64,3,32,flat,128,SkyCeiling::open),"Rays must stop at a grate's solid texels");
    check(traceRay(map,0,64,3,96,flat,128,SkyCeiling::open),"Rays must pass above a grate");

    // A decoration's sprite stands in the courtyard: an 8x64 solid billboard.
    map=rooms({0,128,true,{}},{0,128,true,{}},{0,64,false,{}});
    std::vector<unsigned char> column(8*64*2,255);
    map.things.push_back({80,64,64,-4,8,64,column.data()});map.sectors[0].things.push_back(0);
    check(!sunReachesSky(map,0,40,64,1,east),"A decoration must cast a sun shadow");
    check(sunReachesSky(map,0,40,20,1,east),"Rays beside a decoration must pass");
    BakeHit thingHit;
    check(!traceRay(map,0,40,64,1,east,INFINITY,SkyCeiling::escape,&thingHit)&&thingHit.kind==BakeHit::thing&&thingHit.line==-1,
          "A decoration must be reported as a thing hit");

    BakeHit hit;
    map=rooms({0,128,true,{}},{0,128,true,{}},{0,64,false,{}});
    check(!traceRay(map,2,320,64,1,up,INFINITY,SkyCeiling::open,&hit)&&hit.kind==BakeHit::ceiling&&hit.sector==2&&std::fabs(hit.z-64)<0.01f,
          "An indoor ray must report the ceiling it hits");
    const float down[3]={0,0,-1};
    check(!traceRay(map,0,64,64,50,down,INFINITY,SkyCeiling::open,&hit)&&hit.kind==BakeHit::floor&&std::fabs(hit.z)<0.01f,
          "A downward ray must report the floor");
    check(!traceRay(map,1,140,64,100,east,INFINITY,SkyCeiling::open,&hit)&&hit.kind==BakeHit::wall&&hit.part==1&&hit.sector==1,
          "A facade must be reported as the upper wall facing the ray");

    // A lava pool fills the courtyard floor in two pieces that meet at x=64.
    BakeArea pool;
    pool.pieces={{{0,0},{64,0},{64,128},{0,128}},{{64,0},{128,0},{128,128},{64,128}}};
    pool.low[0]=pool.low[1]=0;pool.high[0]=pool.high[1]=128;pool.radius=256;pool.strength=1;
    auto area=[&](const BakeMap &m,int sector,float x,float z,const float normal[3],float *towards=nullptr) {
        float out[3]={};return addBakedArea(m,pool,sector,x,64,z,normal,out,towards);
    };
    map=rooms({0,128,false,{}},{0,128,false,{}},{0,64,false,{}});
    check(std::fabs(area(map,0,64,40,down)-area(map,0,32,40,down))<1e-5f,"A pool's pieces must light as one, without a seam");
    check(area(map,0,64,40,down)>area(map,1,200,40,down)&&area(map,1,200,40,down)>0,"Pool light must fall off beyond its rim");
    check(area(map,2,383,40,down)==0,"Pool light must end at its radius");
    check(area(map,0,64,-8,floorUp)==0,"Nothing below a pool's surface may receive its light");
    const float wallNormal[3]={-1,0,0};
    check(area(map,0,64,1,wallNormal)<=pool.strength*.5f,"Pool light must not overbrighten walls");
    check(area(map,0,64,1,down)>.5f,"Wall limit must not dim horizontal surfaces");
    float towards[3];area(map,1,200,40,down,towards);
    check(towards[0]<0&&towards[2]<0,"Pool light must arrive from the nearest part of the pool");
    map=rooms({0,128,false,{}},{0,128,false,{}},{40,128,false,{}});
    map.sectors[1].floor=-64;map.sectors[0].floor=-64;pool.z=-64;
    check(area(map,2,270,41,floorUp)==0,"A raised floor must not see a pool far below its lip");
    map.sectors[2].floor=-40;
    check(area(map,2,270,-39,floorUp)>0,"Pool glow must spill over a low rim");
    float direction[3];
    for(float u:{0.0f,0.5f,0.99f})for(float v:{0.0f,0.3f,0.7f}) {
        cosineDirection(flat,u,v,direction);
        check(direction[0]>=0&&std::fabs(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]-1)<1e-4f,
              "Bounce rays must be unit length in the normal's hemisphere");
    }
    std::puts("baked lighting checks passed");
}
