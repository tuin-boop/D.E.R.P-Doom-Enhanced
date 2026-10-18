#ifndef DOOM_PAINTED_TILES_H
#define DOOM_PAINTED_TILES_H
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

// Uneven tiles: the tiles, plates, stones and bricks painted into a flat or
// wall texture, found from its seams so the shader can lay each one a little
// crooked. Seams are dark valleys along lines (or the dark foot of a bevel):
// the height averaged 7 texels along one of four directions is darker than
// both sides 2 texels across, so speckle and small gaps along a seam wash
// out. Flats and walls then group texels into tiles their own way (below).
//
// height: log luminance per texel, w*h, wrapping like the texture. Returns per
// texel the offset to its tile's anchor texel, packed (dx+128)<<8|(dy+128) in
// texture space (unwrapped, so a tile that crosses the texture's edge keeps one
// anchor); noPaintedTile where the texel belongs to no tile. Anchor plus texel
// position is the same for every texel of one tile in one repeat, so it names
// that tile instance in the world.
constexpr uint16_t noPaintedTile=0xFFFF;
// Whether texel (x, y) lies in a seam running along d (dx, dy), d[2..3]
// pointing across it. bevels: the dark foot of a step counts too. widest:
// sides up to this many texels across, so wide gaps count from their middle.
inline bool paintedSeam(const std::vector<float> &height,int w,int h,int x,int y,const int d[4],bool bevels=true,int widest=2) {
    const float valley=0.08f,bevel=0.3f;const int along=3;
    auto line=[&](int ox,int oy) {
        float sum=0;
        for(int k=-along;k<=along;++k) {
            int px=((x+ox+k*d[0])%w+w)%w,py=((y+oy+k*d[1])%h+h)%h;
            sum+=height[(size_t)py*w+px];
        }
        return sum/(2*along+1);
    };
    float c=line(0,0);
    for(int across=2;across<=widest;++across) {
        float a=line(across*d[2],across*d[3]),b=line(-across*d[2],-across*d[3]);
        if(c<std::min(a,b)-valley||(bevels&&c<std::max(a,b)-bevel&&c<=std::min(a,b)+0.02f)) return true;
    }
    return false;
}
inline uint16_t packTileOffset(int dx,int dy) {return (uint16_t)((dx+128)<<8|(dy+128));}
// Flats: tiles of any shape (squares, hexagons, cobbles), seams in four
// directions. Seams thicken by a texel to close what gaps remain, the texels
// between them fall into components, and every seam texel joins the nearest
// one. Grain and organic artwork (noise, marble, lava) is left untiled:
// components must be compact, must not reach themselves across the wrap, and
// seams must cover a modest share of the flat.
inline std::vector<uint16_t> flatTiles(const std::vector<float> &height,int w,int h) {
    size_t count=(size_t)w*h;
    std::vector<uint16_t> anchors(count,noPaintedTile);
    if(w<8||h<8||height.size()!=count) return anchors;
    auto wrap=[](int v,int n){return (v%n+n)%n;};
    auto index=[&](int x,int y){return (size_t)wrap(y,h)*w+wrap(x,w);};
    const int directions[4][4]={{1,0,0,1},{0,1,1,0},{1,1,1,-1},{1,-1,1,1}};
    std::vector<char> thin(count,0),seam(count,0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(const auto &d:directions)
        if(paintedSeam(height,w,h,x,y,d)) {thin[(size_t)y*w+x]=1;break;}
    const int steps[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    size_t thinCount=0,seamCount=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;thinCount+=thin[i];
        bool near=thin[i];for(const auto &s:steps)near=near||thin[index(x+s[0],y+s[1])];
        seam[i]=near;seamCount+=near;
    }
    if(thinCount*10>count*3||seamCount*20>count*11) return anchors;
    // Components of texels between seams; shift counts the flat repeats a
    // texel lies from its component's seed along x and y.
    const int minimum=24;const float compact=0.35f;
    std::vector<int> label(count,-1),owner(count,-1),shiftX(count,0),shiftY(count,0);
    std::vector<size_t> seeds,stack,members;std::vector<char> scrap;
    for(size_t start=0;start<count;++start) {
        if(seam[start]||label[start]>=0) continue;
        int id=(int)seeds.size();seeds.push_back(start);label[start]=id;
        stack.assign(1,start);members.clear();bool wraps=false;
        while(!stack.empty()) {
            size_t i=stack.back();stack.pop_back();members.push_back(i);
            int x=(int)(i%w),y=(int)(i/w);
            for(const auto &s:steps) {
                int nx=x+s[0],ny=y+s[1];
                int kx=shiftX[i]+(nx<0?-1:nx>=w?1:0),ky=shiftY[i]+(ny<0?-1:ny>=h?1:0);
                size_t j=index(nx,ny);
                if(seam[j]) continue;
                if(label[j]<0) {label[j]=id;shiftX[j]=kx;shiftY[j]=ky;stack.push_back(j);}
                else if(shiftX[j]!=kx||shiftY[j]!=ky) wraps=true;
            }
        }
        int x0=1<<30,x1=-(1<<30),y0=1<<30,y1=-(1<<30);
        for(size_t i:members) {
            int ux=(int)(i%w)+shiftX[i]*w,uy=(int)(i/w)+shiftY[i]*h;
            x0=std::min(x0,ux);x1=std::max(x1,ux);y0=std::min(y0,uy);y1=std::max(y1,uy);
        }
        float fill=(float)members.size()/((float)(x1-x0+1)*(float)(y1-y0+1));
        bool good=!wraps&&(int)members.size()>=minimum&&fill>=compact;
        scrap.push_back((int)members.size()<minimum);
        if(good) for(size_t i:members) owner[i]=id;
    }
    // Seams and scraps join the nearest tile, breadth first; grain that
    // failed as a tile stays untiled.
    std::vector<size_t> frontier,next;
    for(size_t i=0;i<count;++i) if(owner[i]>=0) frontier.push_back(i);
    while(!frontier.empty()) {
        next.clear();
        for(size_t i:frontier) {
            int x=(int)(i%w),y=(int)(i/w);
            for(const auto &s:steps) {
                int nx=x+s[0],ny=y+s[1];size_t j=index(nx,ny);
                if(owner[j]>=0||(!seam[j]&&!scrap[label[j]])) continue;
                owner[j]=owner[i];
                shiftX[j]=shiftX[i]+(nx<0?-1:nx>=w?1:0);shiftY[j]=shiftY[i]+(ny<0?-1:ny>=h?1:0);
                next.push_back(j);
            }
        }
        frontier.swap(next);
    }
    for(size_t i=0;i<count;++i) {
        if(owner[i]<0) continue;
        size_t seed=seeds[owner[i]];
        int dx=(int)(seed%w)-((int)(i%w)+shiftX[i]*w),dy=(int)(seed/w)-((int)(i/w)+shiftY[i]*h);
        if(std::abs(dx)>126||std::abs(dy)>126) continue;
        anchors[i]=packTileOffset(dx,dy);
    }
    return anchors;
}
// Walls: tiles are rectangles, rows of plates or bricks (or columns of
// planks) with straight joints, so blotchy artwork never breaks into blobs
// and the joints stay straight. Seams are dark valleys up to 4 texels to
// each side, so the wide gaps between plates count. Seam lines run across
// half the texture, joints down most of their row, and both stand out from
// the other lines, so rust streaks, grain and cobbles do not count. The
// strongest line wins over weaker ones within 16 texels (a plate's inner
// crease), so no tile is thinner than that. A row without joints is one long
// tile: its offset along the row is noTileAxis, so it has no identity there
// and leans only across the row. Rows first; columns first only when no seam
// line runs across. Without seam lines either way nothing is tiled.
constexpr int noTileAxis=127;
inline std::vector<uint16_t> wallTiles(const std::vector<float> &height,int w,int h) {
    size_t count=(size_t)w*h;
    std::vector<uint16_t> none(count,noPaintedTile);
    if(w<16||h<16||height.size()!=count) return none;
    const int alongRows[4]={1,0,0,1},alongColumns[4]={0,1,1,0};
    std::vector<char> rows(count),columns(count);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        rows[(size_t)y*w+x]=paintedSeam(height,w,h,x,y,alongRows,false,4);
        columns[(size_t)y*w+x]=paintedSeam(height,w,h,x,y,alongColumns,false,4);
    }
    const int smallest=16;const float strong=0.5f,joint=0.7f,standout=2.5f;
    // Seam lines around a repeat of n from each line's share of seam texels,
    // strongest first, each keeping weaker lines within smallest out; the
    // spans (start, length) between them.
    auto spans=[&](const std::vector<float> &share,int n,float least) {
        std::vector<std::pair<int,int>> out;std::vector<int> order(n),picked;
        for(int i=0;i<n;++i) order[i]=i;
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){return share[a]>share[b];});
        for(int i:order) {
            if(share[i]<least) break;
            bool clear=true;
            for(int p:picked) {int d=std::abs(p-i);if(std::min(d,n-d)<smallest) {clear=false;break;}}
            if(clear) picked.push_back(i);
        }
        std::sort(picked.begin(),picked.end());
        for(size_t k=0;k<picked.size();++k) {
            int end=k+1<picked.size()?picked[k+1]:picked[0]+n;
            out.push_back({picked[k],end-picked[k]});
        }
        return out;
    };
    auto grid=[&](bool rowsFirst) {
        std::vector<uint16_t> anchors(count,noPaintedTile);
        int n=rowsFirst?h:w,m=rowsFirst?w:h;
        // Line i of the outer axis, position k along it.
        auto at=[&](int i,int k){return rowsFirst?(size_t)i*w+k:(size_t)k*w+i;};
        auto pack=[&](int along,int across){return rowsFirst?packTileOffset(along,across):packTileOffset(across,along);};
        const std::vector<char> &outer=rowsFirst?rows:columns,&inner=rowsFirst?columns:rows;
        std::vector<float> lines(n);
        for(int i=0;i<n;++i) {int hits=0;for(int k=0;k<m;++k)hits+=outer[at(i,k)];lines[i]=(float)hits/m;}
        std::vector<float> ranked=lines;std::nth_element(ranked.begin(),ranked.begin()+n/2,ranked.end());
        auto bands=spans(lines,n,std::max(strong,standout*ranked[n/2]));
        for(auto [start,length]:bands) {
            if(length>128) continue;
            std::vector<float> joints(m);
            for(int k=0;k<m;++k) {int hits=0;for(int j=0;j<length;++j)hits+=inner[at((start+j)%n,k)];joints[k]=(float)hits/length;}
            ranked=joints;std::nth_element(ranked.begin(),ranked.begin()+m/2,ranked.end());
            auto cells=spans(joints,m,std::max(joint,standout*ranked[m/2]));
            for(int j=0;j<length;++j) {
                if(cells.empty()) {for(int k=0;k<m;++k)anchors[at((start+j)%n,k)]=pack(noTileAxis,-j);continue;}
                for(auto [from,width]:cells) if(width<=128)
                    for(int k=0;k<width;++k) anchors[at((start+j)%n,(from+k)%m)]=pack(-k,-j);
            }
        }
        return std::make_pair(anchors,!bands.empty());
    };
    auto [byRows,found]=grid(true);
    if(found) return byRows;
    return grid(false).first;
}

#endif
