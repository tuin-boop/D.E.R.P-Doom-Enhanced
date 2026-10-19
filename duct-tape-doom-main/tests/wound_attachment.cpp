// Frame correspondence checks independent of game data and the renderer.
#include "../platform/wound_attachment.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok,const char *message) {
    if(!ok) {std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
struct Image {
    int w,h,left=0,top=0;
    std::vector<unsigned char> pixels;
    Image(int width,int height):w(width),h(height),pixels((size_t)w*h*2,0) {}
    doom_wounds::Frame frame() const {return {w,h,left,top,pixels.data()};}
    void put(int x,int y,int color) {pixels[((size_t)y*w+x)*2]=(unsigned char)color;pixels[((size_t)y*w+x)*2+1]=255;}
    void rect(int x,int y,int width,int height,int color) {
        for(int r=y;r<y+height;++r)for(int c=x;c<x+width;++c)put(c,r,color);
    }
};
static void near(doom_wounds::Point p,float u,float v,const char *message) {
    check(std::abs(p.u-u)<0.01f&&std::abs(p.v-v)<0.01f,message);
}
static void mark(Image &im,int x,int y) {
    im.rect(x-1,y-1,3,3,70);im.put(x,y,123);im.put(x+1,y,90);im.put(x,y+1,91);
}

int main() {
    using doom_wounds::map;
    Image source(36,46);
    source.rect(10,5,16,35,20);mark(source,20,20);
    near(map(source.frame(),source.frame(),{20.25f,20.75f}),20.25f,20.75f,
         "The hit frame must preserve precise impact coordinates");

    Image shifted(36,46);
    shifted.rect(13,5,16,35,20);mark(shifted,23,20);
    near(map(source.frame(),shifted.frame(),{20.25f,20.75f}),23.25f,20.75f,
         "A sideways sprite sway must carry the hole");

    Image bobbed(36,46);
    bobbed.rect(10,8,16,35,20);mark(bobbed,20,23);
    near(map(source.frame(),bobbed.frame(),{20.25f,20.75f}),20.25f,23.75f,
         "A vertical animation bob must carry the hole");

    // The body bounds stay fixed while a textured patch moves locally.
    // A bounding-box translation cannot pass this check.
    Image movedPart(36,46);
    movedPart.rect(10,5,16,35,20);mark(movedPart,15,25);
    near(map(source.frame(),movedPart.frame(),{20.25f,20.75f}),15.25f,25.75f,
         "A hole must follow nearby artwork within a stationary silhouette");

    Image legs(36,46),stride(36,46);
    legs.rect(8,5,20,20,20);stride.rect(8,5,20,20,20);
    legs.rect(11,25,5,15,30);stride.rect(7,25,5,15,30);
    legs.rect(21,25,5,15,40);stride.rect(21,25,5,15,40);
    mark(legs,13,32);mark(stride,9,32);
    near(map(legs.frame(),stride.frame(),{13.25f,32.75f}),9.25f,32.75f,
         "A striding leg must carry its wound while the torso stays still");

    // Different patch dimensions and sprite origins are not a pose change.
    Image cropped(42,50);
    cropped.left=6;cropped.top=4;
    cropped.rect(16,9,16,35,20);mark(cropped,26,24);
    near(map(source.frame(),cropped.frame(),{20.25f,20.75f}),26.25f,24.75f,
         "Attachment must survive changed dimensions and sprite offsets");

    Image crouched(36,46);
    crouched.rect(10,15,16,25,20);mark(crouched,20,26);
    near(map(source.frame(),crouched.frame(),{20.25f,20.75f}),20.25f,26.75f,
         "A crouching silhouette must move the wound down with its artwork");

    Image plain(36,46),plainMoved(36,46);
    plain.rect(10,5,16,35,20);plainMoved.rect(13,8,16,35,20);
    near(map(plain.frame(),plainMoved.frame(),{18.25f,20.75f}),21.25f,23.75f,
         "Untextured artwork must use the silhouette instead of drifting");
    Image empty(36,46);
    near(map(source.frame(),empty.frame(),{20.25f,20.75f}),20.25f,20.75f,
         "An empty frame must return a finite origin-relative fallback");
    return 0;
}
