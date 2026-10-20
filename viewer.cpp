#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include "scene.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstring>
using namespace lab;
static Scene scene;
static Volume &world=scene.terrain;
static GLuint doorMesh=0;static unsigned doorRevision=~0u;
static HWND window;static HDC dc;static HGLRC context;
static Vec feet;static float yaw,pitch=0,fall=0;static bool captured=false,running=true;
static GLuint mesh=0,font=0,textures[2];static unsigned drawnRevision=~0u;
static std::string status="Independent geometry lab - original game and saves are untouched";
static int width=1100,height=760;static float shotCooldown=0;
static void lockMouse(bool on){captured=on;ShowCursor(on?FALSE:TRUE);if(on)SetCapture(window);else ReleaseCapture();}
static bool load(const char *file){
 if(!scene.load(file)){status=std::string("Could not load ")+file;return false;}
 feet=world.spawn;yaw=world.yaw;pitch=0;fall=0;drawnRevision=doorRevision=~0u;
 if(!scene.fits(feet)){
  Vec original=feet;bool found=false;
  for(int r=0;r<=64&&!found;r+=8)for(int dz=-32;dz<=32&&!found;dz+=4)for(int d=0;d<8&&!found;++d){
   float a=d*6.2831853f/8;Vec p=original+Vec{r*std::cos(a),r*std::sin(a),float(dz)};
   if(scene.fits(p)){feet=p;found=true;}
  }
  if(!found){status="No safe spawn found in this fixture";return false;}
 }
 status=std::string("Loaded ")+file;return true;
}
static void compileVolume(const Volume& volume,GLuint& list,unsigned& revision){
 if(revision==volume.revision)return;revision=volume.revision;
 if(!list)list=glGenLists(1);glNewList(list,GL_COMPILE);
 for(int damaged=0;damaged<2;++damaged){
  glBindTexture(GL_TEXTURE_2D,textures[damaged]);glBegin(GL_TRIANGLES);
  for(const auto&chunk:volume.chunks)for(const auto&t:chunk.triangles)if(t.damaged==bool(damaged)){
   float light=.45f+.32f*std::abs(t.normal.z)+.16f*std::abs(t.normal.x);
   glColor3f(light,light,light);
   for(Vec p:{t.a,t.b,t.c}){
    if(std::abs(t.normal.z)>.6f)glTexCoord2f(p.x/32,p.y/32);
    else if(std::abs(t.normal.x)>std::abs(t.normal.y))glTexCoord2f(p.y/32,p.z/32);
    else glTexCoord2f(p.x/32,p.z/32);
    glVertex3f(p.x,p.y,p.z);
   }
  }glEnd();
 }glEndList();
}
static void text(float x,float y,const std::string&s){glRasterPos2f(x,y);glListBase(font);glCallLists(int(s.size()),GL_UNSIGNED_BYTE,s.c_str());}
static void draw(){
 compileVolume(world,mesh,drawnRevision);
 if(scene.hasDoor)compileVolume(scene.door,doorMesh,doorRevision);
 glViewport(0,0,width,height);glClearColor(.075f,.085f,.10f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(75,double(width)/height,.5,1800);
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();Vec eye=feet+Vec{0,0,41},dir={std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),std::sin(pitch)};
 gluLookAt(eye.x,eye.y,eye.z,eye.x+dir.x,eye.y+dir.y,eye.z+dir.z,0,0,1);
 glEnable(GL_DEPTH_TEST);glEnable(GL_TEXTURE_2D);glCallList(mesh);
 if(scene.hasDoor){glPushMatrix();glTranslatef(0,0,scene.doorZ);glCallList(doorMesh);glPopMatrix();}
 glDisable(GL_TEXTURE_2D);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,width,height,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);
 glColor3f(.75f,.95f,.8f);glBegin(GL_LINES);glVertex2i(width/2-5,height/2);glVertex2i(width/2+5,height/2);glVertex2i(width/2,height/2-5);glVertex2i(width/2,height/2+5);glEnd();
 glColor3f(.07f,.09f,.12f);glBegin(GL_QUADS);glVertex2i(0,0);glVertex2i(width,0);glVertex2i(width,76);glVertex2i(0,76);glEnd();
 glColor3f(.88f,.91f,.93f);
 text(14,22,"SOLID VOLUME LAB | WASD walk | Mouse look | Click blast | Space jump | Esc release mouse");
 text(14,44,"1/2: saved fixtures | 3: moving door test | E: door | F5/F9: save/load | No enemies");
 char stats[160];std::snprintf(stats,sizeof stats,"%zu triangles | Last edit rebuilt %d / %zu chunks | ",world.triangleCount(),world.lastRebuilt,world.chunks.size());
 text(14,66,std::string(stats)+status);
}
static void capture(){
 std::vector<unsigned char> pixels(size_t(width)*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
 int stride=(width*3+3)&~3;std::vector<unsigned char> bmp(size_t(stride)*height);
 for(int y=0;y<height;++y)for(int x=0;x<width;++x){size_t a=(size_t(y)*width+x)*3,b=size_t(y)*stride+x*3;bmp[b]=pixels[a+2];bmp[b+1]=pixels[a+1];bmp[b+2]=pixels[a];}
 BITMAPFILEHEADER fh{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+DWORD(bmp.size());
 BITMAPINFOHEADER ih{};ih.biSize=sizeof ih;ih.biWidth=width;ih.biHeight=height;ih.biPlanes=1;ih.biBitCount=24;
 std::ofstream f("preview.bmp",std::ios::binary);f.write((char*)&fh,sizeof fh);f.write((char*)&ih,sizeof ih);f.write((char*)bmp.data(),bmp.size());
}
static void update(float dt){
 scene.tick(dt,feet);
 if(!captured)return;
 POINT mid{width/2,height/2},pos;ClientToScreen(window,&mid);GetCursorPos(&pos);
 yaw-=(pos.x-mid.x)*.0025f;pitch=std::clamp(pitch-(pos.y-mid.y)*.0025f,-1.45f,1.45f);SetCursorPos(mid.x,mid.y);
 auto down=[](int k){return (GetAsyncKeyState(k)&0x8000)!=0;};
 float f=float(down('W'))-float(down('S')),r=float(down('D'))-float(down('A'));
 Vec delta={std::cos(yaw)*f+std::sin(yaw)*r,std::sin(yaw)*f-std::cos(yaw)*r,0};
 float len=length(delta);if(len>0)feet=scene.move(feet,delta*(100*dt/len));
 Vec before=feet;feet=scene.move(feet,{0,0,-.3f});bool grounded=std::abs(feet.z-before.z)<.05f;
 if(grounded){fall=0;if(down(VK_SPACE))fall=140;}
 else fall-=360*dt;
 before=feet;feet=scene.move(feet,{0,0,fall*dt});if(std::abs(feet.z-before.z)<.001f&&fall<0)fall=0;
 shotCooldown-=dt;
 if(down(VK_LBUTTON)&&shotCooldown<=0){
  shotCooldown=.25f;Vec eye=feet+Vec{0,0,41},dir={std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),std::sin(pitch)};Hit hit;int owner;
  if(scene.ray(eye,dir,1200,hit,owner)){
   auto start=std::chrono::steady_clock::now();bool changed=scene.blast(hit.point-hit.normal*7,18,owner);
   auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   char s[100];std::snprintf(s,sizeof s,changed?"Blast rebuilt nearby surface in %.1f ms":"Intact test boundary (%.1f ms)",ms);status=s;
  }
 }
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_SIZE){width=std::max(1,int(LOWORD(l)));height=std::max(1,int(HIWORD(l)));return 0;}
 if(m==WM_CLOSE){running=false;return 0;}
 if(m==WM_KILLFOCUS){if(captured)lockMouse(false);return 0;}
 if(m==WM_LBUTTONDOWN&&!captured){lockMouse(true);POINT p{width/2,height/2};ClientToScreen(h,&p);SetCursorPos(p.x,p.y);shotCooldown=.3f;return 0;}
 if(m==WM_KEYDOWN){
  if(w==VK_ESCAPE){if(captured)lockMouse(false);else running=false;}
  if(w=='1')load("linedef5.svl");if(w=='2')load("linedef6.svl");
  if(w=='3'){scene.createDoorTest();feet=world.spawn;yaw=world.yaw;pitch=fall=0;drawnRevision=doorRevision=~0u;status="Moving door test: E raises/closes; blast damage follows door";}
  if(w=='E'&&!(l&(1<<30)))scene.toggleDoor();
  if(w==VK_F5){world.spawn=feet;world.yaw=yaw;status=scene.save("lab-scene.svs")?"Saved lab-scene.svs":"Save failed";}
  if(w==VK_F9)load("lab-scene.svs");return 0;
 }
 return DefWindowProc(h,m,w,l);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int){
 char exe[MAX_PATH];GetModuleFileNameA(nullptr,exe,MAX_PATH);std::string dir=exe;dir=dir.substr(0,dir.find_last_of("\\/"));SetCurrentDirectoryA(dir.c_str());
 bool screenshot=std::strstr(GetCommandLineA(),"--capture")!=nullptr;
 WNDCLASSA cls{};cls.style=CS_OWNDC;cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName="DERPSolidLab";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&cls);
 window=CreateWindowA(cls.lpszClassName,"DERP - Solid Volume Prototype",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,width,height,nullptr,nullptr,instance,nullptr);
 RECT client;GetClientRect(window,&client);width=client.right;height=client.bottom;
 dc=GetDC(window);PIXELFORMATDESCRIPTOR pf{sizeof pf,1};pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=24;pf.cDepthBits=24;SetPixelFormat(dc,ChoosePixelFormat(dc,&pf),&pf);
 context=wglCreateContext(dc);if(!context||!wglMakeCurrent(dc,context))return 1;
 font=glGenLists(256);HFONT hf=CreateFontA(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FF_DONTCARE,"Consolas");SelectObject(dc,hf);wglUseFontBitmapsA(dc,0,256,font);
 glGenTextures(2,textures);
 for(int k=0;k<2;++k){std::vector<unsigned char> data(64*64*3);
  for(int y=0;y<64;++y)for(int x=0;x<64;++x){unsigned hash=(x*73856093u)^(y*19349663u);int n=int(hash%35)-17;
   int value=k?140+n:108+n/2;if(!k&&(x%32<2||y%16<2))value-=25;
   for(int c=0;c<3;++c)data[(y*64+x)*3+c]=(unsigned char)std::clamp(value+(k?0:(c==0?12:c==2?-8:3)),0,255);
  }
  glBindTexture(GL_TEXTURE_2D,textures[k]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,64,64,0,GL_RGB,GL_UNSIGNED_BYTE,data.data());
 }
 if(!load(std::strstr(GetCommandLineA(),"--fixture6")?"linedef6.svl":"linedef5.svl")){MessageBoxA(window,status.c_str(),"Volume lab",MB_OK);return 1;}
 if(std::strstr(GetCommandLineA(),"--demo-blast")){
  for(float x:{1344.f,1360.f,1376.f})world.blast({x,-3456,32},36);
  world.blast({1352,-3456,0},24);world.blast({1352,-3456,72},24);
  yaw=0;status="QA view: combined door, floor and ceiling excavation";
 }
 if(std::strstr(GetCommandLineA(),"--door-test")){scene.createDoorTest();feet=world.spawn;yaw=0;drawnRevision=doorRevision=~0u;scene.door.blast({112,96,42},18);scene.doorZ=24;status="QA moving door: damaged surface raised 24 units";}
 if(!screenshot){scene.createDoorTest();feet=world.spawn;yaw=0;pitch=fall=0;drawnRevision=doorRevision=~0u;status="Moving door test: E raises/closes; blast damage follows door";ShowWindow(window,SW_SHOW);}
 auto last=std::chrono::steady_clock::now();int frames=0;
 while(running){MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}
  auto now=std::chrono::steady_clock::now();float dt=std::clamp(std::chrono::duration<float>(now-last).count(),.001f,.04f);last=now;update(dt);draw();
  if(screenshot&&++frames==3){glFinish();capture();running=false;}
  SwapBuffers(dc);Sleep(1);
 }
 if(captured)lockMouse(false);wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);DeleteObject(hf);return 0;
}
