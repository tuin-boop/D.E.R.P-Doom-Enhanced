/* The in-game F4 graphics panel. It draws into its own 640x400 image on a
 * grid of 8x16 cells, like a text-mode setup program, in the WAD's own small
 * font (STCFN), uppercase as in DOOM's messages; the renderer scales it by
 * whole pixels over the dimmed view. Changes apply when the panel closes. */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
extern "C" {
#include "doomtype.h"
#include "doomdef.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomstat.h"
#include "g_game.h"
extern boolean paused;
}
#include <SDL3/SDL.h>
#include "i_render3d.h"
#include "scene3d.h"
#include "settings_panel.h"

using namespace doom3d;

namespace {
// One row of the current tab: a section heading (option null) or an option.
struct Line { const OptionSection *section; Option *option; };

struct Panel {
    bool open=false,wasPaused=false;
    Settings draft;
    std::vector<OptionTab> tabs;
    std::vector<Line> lines;
    int tab=0,selected=0,scroll=0,firstTab=0;
    std::vector<std::pair<int,int>> tabSpans; // left and right pixel of each tab's bar
} panel;
static int selectedMap=0;
static std::vector<std::string> mapNames;
static std::vector<std::pair<int,int>> mapNumbers;
void closePanel();
void startSelectedMap(){
    if(selectedMap<0||selectedMap>=(int)mapNumbers.size())return;
    auto map=mapNumbers[selectedMap];closePanel();
    G_DeferedInitNew(gameskill,map.first,map.second);
}
void addMapTab(){
    mapNames.clear();mapNumbers.clear();selectedMap=0;
    for(int episode=1;episode<=(gamemode==commercial?1:9);++episode)
        for(int map=1;map<=(gamemode==commercial?32:9);++map){
            char name[9];if(gamemode==commercial)snprintf(name,sizeof(name),"MAP%02d",map);else snprintf(name,sizeof(name),"E%dM%d",episode,map);
            if(W_CheckNumForName(name)<0)continue;
            if(episode==gameepisode&&map==gamemap)selectedMap=(int)mapNames.size();
            mapNames.emplace_back(name);mapNumbers.emplace_back(episode,map);
        }
    if(mapNames.empty())return;
    Option map{};map.kind=OptionKind::Choice;map.key="";map.title="Map";map.tip="Choose a loaded map, then select Start map. This starts a new game on that map.";map.flag=&selectedMap;
    for(size_t n=0;n<mapNames.size();++n){map.items.push_back(mapNames[n].c_str());map.values.push_back((int)n);}
    Option start{};start.kind=OptionKind::Action;start.key="";start.title="Start map";start.tip="Start the selected map at the current difficulty. Current level progress will be replaced.";start.action=startSelectedMap;
    panel.tabs.insert(panel.tabs.begin(),{"Game",{{"Change map",{map,start}}}});
}

// The image is a grid of 8x16 character cells, 80 columns by 25 rows.
const int width=DOOM_SETTINGS_WIDTH,height=DOOM_SETTINGS_HEIGHT,cellWidth=8,cellHeight=16;
const int tabRow=0,listRow=2,visibleLines=17,descriptionRow=20,descriptionLines=4,helpRow=24;
const int leftColumn=2,rightColumn=78,sliderColumns=20,valueColumns=6;

// Colors as 0xAARRGGBB, the byte order of the B8G8R8A8 texture.
const unsigned backdrop=0xc8080808,separator=0xff3a3a3a,text=0xffe6e6e6,heading=0xffe8b040,dim=0xff686868,
               description=0xffb4b4b4,selectionBar=0xff7a1414,bright=0xffffffff,onColor=0xff8cd48c,
               tabBar=0xffa01818,track=0xff505050,trackFill=0xffd03030;

unsigned *image;

void fill(int x,int y,int w,int h,unsigned color) {
    for(int row=std::max(0,y);row<std::min(height,y+h);++row)
        std::fill(image+row*width+std::max(0,x),image+row*width+std::min(width,x+w),color);
}

// The HUD font, STCFN033 ('!') to STCFN095 ('_'), as brightness masks: 0 is
// transparent, otherwise the palette color's brightness relative to the
// font's brightest pixel, so the artwork's shading survives any text color.
struct Glyph { int width=0,height=0,left=0,top=0;std::vector<unsigned char> shade; };
const int firstGlyph='!',lastGlyph='_',spaceWidth=4,glyphHeight=7;
std::vector<Glyph> glyphs;

void loadFont() {
    if(!glyphs.empty())return;
    glyphs.resize(lastGlyph-firstGlyph+1);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<int> brightness(256);
    for(int i=0;i<256;++i)brightness[i]=palette[3*i]*299+palette[3*i+1]*587+palette[3*i+2]*114;
    std::vector<std::vector<int>> raw(glyphs.size());
    int brightest=1;
    for(int c=firstGlyph;c<=lastGlyph;++c) {
        char name[9];snprintf(name,sizeof(name),"STCFN%03d",c);
        int lump=W_CheckNumForName(name);
        if(lump<0)continue;
        const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
        int size=W_LumpLength(lump);
        if(size<8)continue;
        Glyph &glyph=glyphs[(size_t)(c-firstGlyph)];
        int w=(short)(data[0]|data[1]<<8),h=(short)(data[2]|data[3]<<8);
        if(w<=0||h<=0||w>32||h>32||8+4*w>size)continue;
        std::vector<int> &pixels=raw[(size_t)(c-firstGlyph)];
        pixels.assign((size_t)(w*h),-1);
        // Patch columns: posts of (top, length, pad, pixels, pad) up to 0xff.
        for(int x=0;x<w;++x) {
            int at=data[8+4*x]|data[9+4*x]<<8|data[10+4*x]<<16|data[11+4*x]<<24;
            while(at>=0&&at<size&&data[at]!=0xff) {
                if(at+2>=size)break;
                int top=data[at],length=data[at+1];
                if(at+3+length>size)break;
                for(int y=0;y<length;++y)if(top+y<h) {
                    int index=data[at+3+y];
                    pixels[(size_t)((top+y)*w+x)]=index;
                    brightest=std::max(brightest,brightness[(size_t)index]);
                }
                at+=length+4;
            }
        }
        glyph.width=w;glyph.height=h;
        glyph.left=(short)(data[4]|data[5]<<8);glyph.top=(short)(data[6]|data[7]<<8);
    }
    for(size_t i=0;i<glyphs.size();++i) {
        Glyph &glyph=glyphs[i];
        glyph.shade.resize(raw[i].size());
        for(size_t p=0;p<raw[i].size();++p)
            glyph.shade[p]=raw[i][p]<0?0:(unsigned char)std::max(1,brightness[(size_t)raw[i][p]]*255/brightest);
    }
}

const Glyph *glyphFor(char c) {
    int upper=std::toupper((unsigned char)c);
    if(upper<firstGlyph||upper>lastGlyph)return nullptr;
    const Glyph &glyph=glyphs[(size_t)(upper-firstGlyph)];
    return glyph.width?&glyph:nullptr;
}

int textWidth(const std::string &string) {
    int w=0;
    for(char c:string) {const Glyph *glyph=glyphFor(c);w+=glyph?glyph->width:spaceWidth;}
    return w;
}

unsigned shaded(unsigned color,int shade) {
    unsigned r=(color>>16&255)*shade/255,g=(color>>8&255)*shade/255,b=(color&255)*shade/255;
    return (color&0xff000000)|r<<16|g<<8|b;
}

// Draws from pixel x, vertically centered in the row's cell; returns the end.
int drawTextAt(const std::string &string,int x,int row,unsigned color) {
    int baseline=row*cellHeight+(cellHeight-glyphHeight)/2;
    for(char c:string) {
        const Glyph *glyph=glyphFor(c);
        if(!glyph) {x+=spaceWidth;continue;}
        int left=x-glyph->left,top=baseline-glyph->top;
        for(int y=0;y<glyph->height;++y)
            for(int i=0;i<glyph->width;++i) {
                int shade=glyph->shade[(size_t)(y*glyph->width+i)],px=left+i,py=top+y;
                if(shade&&px>=0&&px<width&&py>=0&&py<height)image[py*width+px]=shaded(color,shade);
            }
        x+=glyph->width;
    }
    return x;
}

void drawText(const std::string &string,int column,int row,unsigned color) { drawTextAt(string,column*cellWidth,row,color); }

// Ends the text at the left edge of column.
void drawTextRight(const std::string &string,int column,int row,unsigned color) {
    drawTextAt(string,column*cellWidth-textWidth(string),row,color);
}

int rowY(int row) { return row*cellHeight; }

void drawSeparator(int row) { fill(leftColumn*cellWidth,rowY(row)+cellHeight/2,(rightColumn-leftColumn)*cellWidth,1,separator); }

// Splits text into lines at most lineWidth pixels wide; the last line ends
// in "..." when the text does not fit.
std::vector<std::string> wrap(const std::string &string,int lineWidth,size_t maxLines) {
    std::vector<std::string> lines;
    std::string line,word;
    auto flushWord=[&] {
        if(word.empty())return;
        if(line.empty())line=word;
        else if(textWidth(line+" "+word)<=lineWidth)line+=" "+word;
        else {lines.push_back(line);line=word;}
        word.clear();
    };
    for(char c:string) {if(c==' ')flushWord();else word+=c;}
    flushWord();
    if(!line.empty())lines.push_back(line);
    if(lines.size()>maxLines) {
        lines.resize(maxLines);
        std::string &last=lines.back();
        while(!last.empty()&&textWidth(last+"...")>lineWidth)last.pop_back();
        last+="...";
    }
    return lines;
}

std::string valueText(const Option &option) {
    if(option.kind==OptionKind::Action)return "Start";
    if(option.kind==OptionKind::Check)return *option.flag?"On":"Off";
    if(option.kind==OptionKind::Choice) {
        for(size_t i=0;i<option.values.size();++i)if(option.values[i]==*option.flag)return option.items[i];
        return option.items.front();
    }
    size_t step=(size_t)std::lround((*option.number-option.low)/option.step);
    if(step<option.items.size())return option.items[step];
    char value[16];
    snprintf(value,sizeof(value),option.step>=1?"%.0f":"%.2f",*option.number);
    return value;
}

void selectTab(int tab) {
    int count=(int)panel.tabs.size();
    panel.tab=(tab%count+count)%count;
    panel.lines.clear();
    for(OptionSection &section:panel.tabs[(size_t)panel.tab].sections) {
        panel.lines.push_back({&section,nullptr});
        for(Option &option:section.options)panel.lines.push_back({&section,&option});
    }
    panel.selected=1;panel.scroll=0;
}

void keepSelectionVisible() {
    // The first option brings its section heading into view too.
    if(panel.selected==1)panel.scroll=0;
    if(panel.selected<panel.scroll)panel.scroll=panel.selected;
    if(panel.selected>=panel.scroll+visibleLines)panel.scroll=panel.selected-visibleLines+1;
}

void moveSelection(int direction) {
    int count=(int)panel.lines.size();
    do panel.selected=((panel.selected+direction)%count+count)%count;
    while(!panel.lines[(size_t)panel.selected].option);
    keepSelectionVisible();
    S_StartSound(NULL,sfx_pstop);
}

Option &selectedOption() { return *panel.lines[(size_t)panel.selected].option; }

void setSlider(Option &option,double value) { *option.number=option.snap(value); }

void changeOption(int direction) {
    Option &option=selectedOption();
    if(!option.isEnabled(panel.draft)) {S_StartSound(NULL,sfx_oof);return;}
    if(option.kind==OptionKind::Action){if(direction>0&&option.action)option.action();return;}
    if(option.kind==OptionKind::Check)*option.flag=!*option.flag;
    else if(option.kind==OptionKind::Choice) {
        int count=(int)option.values.size(),index=0;
        for(int i=0;i<count;++i)if(option.values[(size_t)i]==*option.flag)index=i;
        *option.flag=option.values[(size_t)(((index+direction)%count+count)%count)];
    } else setSlider(option,*option.number+direction*option.step);
    S_StartSound(NULL,sfx_stnmov);
}

void closePanel() {
    settings=panel.draft;
    paused=panel.wasPaused;
    panel.open=false;
    settingsChanged();
    S_StartSound(NULL,sfx_swtchx);
}

int sliderLeft() { return (rightColumn-valueColumns-sliderColumns)*cellWidth; }
int sliderWidth() { return (sliderColumns-1)*cellWidth; }

// A small triangle in the column right of the list, pointing where more options are.
void drawArrow(int row,bool up) {
    int x=(rightColumn+1)*cellWidth+cellWidth/2,y=rowY(row)+(cellHeight-4)/2;
    for(int i=0;i<4;++i)fill(x-(up?i:3-i),y+i,2*(up?i:3-i)+1,1,dim);
}

void drawSlider(const Option &option,int row,bool enabled) {
    int x=sliderLeft(),w=sliderWidth(),y=rowY(row)+cellHeight/2;
    int position=(int)std::lround((*option.number-option.low)/(option.high-option.low)*(w-1));
    fill(x,y-1,w,2,track);
    fill(x,y-1,position,2,enabled?trackFill:dim);
    fill(x+position-2,y-5,5,10,enabled?bright:dim);
}
}

void I_Render3DSettingsMenu(void) {
    if(panel.open)return;
    panel.draft=settings;
    if(auto *window=SDL_GetKeyboardFocus())panel.draft.fullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0;
    panel.tabs=optionTabs(panel.draft);
    addMapTab();
    selectTab(panel.tab);
    panel.wasPaused=paused;paused=true;
    panel.open=true;
    S_StartSound(NULL,sfx_swtchn);
}

int I_Render3DSettingsOpen(void) { return panel.open; }

void I_Render3DSettingsKey(int scancode,int shift) {
    if(!panel.open)return;
    switch(scancode) {
    case SDL_SCANCODE_UP: moveSelection(-1); break;
    case SDL_SCANCODE_DOWN: moveSelection(1); break;
    case SDL_SCANCODE_LEFT: changeOption(-1); break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
        changeOption(1); break;
    case SDL_SCANCODE_TAB: selectTab(panel.tab+(shift?-1:1)); S_StartSound(NULL,sfx_pstop); break;
    case SDL_SCANCODE_PAGEUP: selectTab(panel.tab-1); S_StartSound(NULL,sfx_pstop); break;
    case SDL_SCANCODE_PAGEDOWN: selectTab(panel.tab+1); S_StartSound(NULL,sfx_pstop); break;
    case SDL_SCANCODE_R: panel.draft=Settings{}; S_StartSound(NULL,sfx_stnmov); break;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_F4: case SDL_SCANCODE_BACKSPACE: closePanel(); break;
    default:
        if(scancode>=SDL_SCANCODE_1&&scancode<SDL_SCANCODE_1+(int)panel.tabs.size()) {
            selectTab(scancode-SDL_SCANCODE_1);S_StartSound(NULL,sfx_pstop);
        }
        break;
    }
}

void I_Render3DSettingsClick(int x,int y,int button) {
    if(!panel.open||x<0||y<0||x>=width||y>=height)return;
    int row=y/cellHeight;
    if(row==tabRow) {
        if(button!=1)return;
        if(x<4*cellWidth){selectTab(panel.tab-1);return;}
        if(x>=width-4*cellWidth){selectTab(panel.tab+1);return;}
        for(size_t i=0;i<panel.tabSpans.size();++i)
            if(x>=panel.tabSpans[i].first&&x<panel.tabSpans[i].second) {
                if((int)i!=panel.tab) {selectTab((int)i);S_StartSound(NULL,sfx_pstop);}
                return;
            }
        return;
    }
    int index=panel.scroll+row-listRow;
    if(row<listRow||row>=listRow+visibleLines||index>=(int)panel.lines.size()||!panel.lines[(size_t)index].option)return;
    panel.selected=index;
    if(button==0)return; // Hovering highlights the option.
    Option &option=selectedOption();
    if(option.kind==OptionKind::Slider&&button==1&&x>=sliderLeft()-cellWidth) {
        if(!option.isEnabled(panel.draft)) {S_StartSound(NULL,sfx_oof);return;}
        double fraction=(double)(x-sliderLeft())/(sliderWidth()-1);
        setSlider(option,option.low+std::clamp(fraction,0.0,1.0)*(option.high-option.low));
        S_StartSound(NULL,sfx_stnmov);
    } else changeOption(button==1?1:-1);
}

void I_Render3DSettingsDraw(unsigned *pixels) {
    if(!panel.open)return;
    image=pixels;
    loadFont();
    fill(0,0,width,height,backdrop);

    // Tabs across the top, the current one on a red bar.
    panel.tabSpans.assign(panel.tabs.size(),{-1,-1});
    panel.firstTab=std::min(panel.firstTab,panel.tab);
    for(;;){int required=0;for(int i=panel.firstTab;i<=panel.tab;++i)required+=textWidth(panel.tabs[i].title)+3*cellWidth;
        if(required<=width-8*cellWidth||panel.firstTab==panel.tab)break;++panel.firstTab;}
    int x=4*cellWidth;
    drawText("<",leftColumn,tabRow,description);drawText(">",rightColumn-1,tabRow,description);
    for(size_t i=panel.firstTab;i<panel.tabs.size();++i) {
        std::string title=panel.tabs[i].title;
        bool current=(int)i==panel.tab;
        int w=textWidth(title);
        if(x+w+cellWidth>width-4*cellWidth)break;
        if(current)fill(x-cellWidth,rowY(tabRow),w+2*cellWidth,cellHeight,tabBar);
        drawTextAt(title,x,tabRow,current?bright:description);
        panel.tabSpans[i]={x-cellWidth,x+w+cellWidth};
        x+=w+3*cellWidth;
    }
    drawSeparator(tabRow+1);

    for(int i=0;i<visibleLines&&panel.scroll+i<(int)panel.lines.size();++i) {
        const Line &line=panel.lines[(size_t)(panel.scroll+i)];
        int row=listRow+i;
        if(!line.option) {drawText(line.section->title,leftColumn,row,heading);continue;}
        const Option &option=*line.option;
        bool selected=panel.scroll+i==panel.selected,enabled=option.isEnabled(panel.draft);
        if(selected)fill((leftColumn+1)*cellWidth,rowY(row),(rightColumn-leftColumn)*cellWidth,cellHeight,selectionBar);
        unsigned color=!enabled?dim:selected?bright:text;
        drawText(option.title,leftColumn+2,row,color);
        unsigned valueColor=enabled&&option.kind==OptionKind::Check&&*option.flag?onColor:color;
        drawTextRight(valueText(option),rightColumn,row,valueColor);
        if(option.kind==OptionKind::Slider)drawSlider(option,row,enabled);
    }
    if(panel.scroll>0)drawArrow(listRow,true);
    if(panel.scroll+visibleLines<(int)panel.lines.size())drawArrow(listRow+visibleLines-1,false);
    drawSeparator(descriptionRow-1);

    const Option &option=selectedOption();
    std::vector<std::string> lines=wrap(option.tip?option.tip:"",(rightColumn-leftColumn)*cellWidth,descriptionLines);
    for(size_t i=0;i<lines.size();++i)drawText(lines[i],leftColumn,descriptionRow+(int)i,description);
    std::string help="Up/Down select   Left/Right change   Tab page   R defaults   Esc done";
    drawTextAt(help,(width-textWidth(help))/2,helpRow,dim);
    // The renderer repeats the border texels beyond the image, so they stay plain backdrop.
    fill(0,0,width,1,backdrop);fill(0,height-1,width,1,backdrop);
    fill(0,0,1,height,backdrop);fill(width-1,0,1,height,backdrop);
}

void I_Render3DLoadingDraw(unsigned *pixels,const char *title,const char *stage,float fraction) {
    image=pixels;
    loadFont();
    const unsigned black=0xff000000;
    fill(0,0,width,height,black);
    const int titleRow=11,barY=rowY(titleRow+1)+4,barWidth=320,barX=(width-barWidth)/2,barHeight=6;
    drawTextAt(title,(width-textWidth(title))/2,titleRow,heading);
    fill(barX-2,barY-2,barWidth+4,barHeight+4,track);
    fill(barX-1,barY-1,barWidth+2,barHeight+2,black);
    fill(barX,barY,(int)std::lround(barWidth*std::clamp(fraction,0.0f,1.0f)),barHeight,trackFill);
    drawTextAt(stage,(width-textWidth(stage))/2,titleRow+2,description);
}

void I_Render3DSettings(void) { I_Render3DSettingsMenu(); }
