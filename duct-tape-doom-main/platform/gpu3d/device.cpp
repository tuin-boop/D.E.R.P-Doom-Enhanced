/* The SDL_gpu device: creation and shutdown, samplers, the fixed textures,
   texture uploads, and the texture hooks the scene builds with (scene3d.h). */
#include "internal.h"

namespace gpu3d {
SDL_GPUDevice *device;
SDL_GPUShaderFormat shaderFormat;
static SDL_GPUCommandBuffer *uploadCommand; // Texture uploads, submitted before the frame that uses them.
SDL_GPUTextureFormat depthFormat,swapchainFormat;
int maxSamples=1;bool forceSingleSample=false;
SDL_GPUSampler *linearRepeat,*linearClamp,*nearestClamp;
SDL_GPUTexture *settingsTexture,*hudTexture,*paletteTexture,*paletteLUT,*dummySeam,*dummyContact,*dummyBake,*dummyRelief,*solidTexel;
static std::array<unsigned,256> paletteKey;
SDL_GPUTexture *createTexture(SDL_GPUTextureFormat format,int w,int h,SDL_GPUTextureUsageFlags usage,
                              SDL_GPUSampleCount samples,SDL_GPUTextureType type,int depth) {
    SDL_GPUTextureCreateInfo info={};
    info.type=type;info.format=format;info.usage=usage;info.width=(Uint32)w;info.height=(Uint32)h;
    info.layer_count_or_depth=(Uint32)depth;info.num_levels=1;info.sample_count=samples;
    return SDL_CreateGPUTexture(device,&info);
}
// Uploads go through their own command buffer so they can be recorded at any
// point, even while the frame is inside a render pass.
static void upload(SDL_GPUTexture *texture,int w,int h,int depth,const void *pixels,Uint32 bytes,Uint32 pixelsPerRow) {
    SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes,0};
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(device,&info);
    if(!transfer)I_Error((char*)"Could not allocate a GPU upload: %s",SDL_GetError());
    memcpy(SDL_MapGPUTransferBuffer(device,transfer,false),pixels,bytes);
    SDL_UnmapGPUTransferBuffer(device,transfer);
    if(!uploadCommand)uploadCommand=SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(uploadCommand);
    SDL_GPUTextureTransferInfo source={transfer,0,pixelsPerRow,(Uint32)h};
    SDL_GPUTextureRegion target={};
    target.texture=texture;target.w=(Uint32)w;target.h=(Uint32)h;target.d=(Uint32)depth;
    SDL_UploadToGPUTexture(copy,&source,&target,true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device,transfer);
}
void flushUploads() {
    if(uploadCommand&&!SDL_SubmitGPUCommandBuffer(uploadCommand))
        fprintf(stderr,"GPU upload failed: %s\n",SDL_GetError());
    uploadCommand=nullptr;
}
void updatePaletteLUT(const unsigned *palette) {
    if(paletteLUT&&std::equal(palette,palette+256,paletteKey.begin()))return;
    std::copy(palette,palette+256,paletteKey.begin());
    std::vector<unsigned> colors=paletteLUTColors(palette);
    if(!paletteLUT)paletteLUT=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,32,32,SDL_GPU_TEXTUREUSAGE_SAMPLER,
        SDL_GPU_SAMPLECOUNT_1,SDL_GPU_TEXTURETYPE_3D,32);
    if(!paletteLUT)I_Error((char*)"Could not allocate palette lookup");
    upload(paletteLUT,32,32,32,colors.data(),(Uint32)(colors.size()*4),32);
}
static SDL_GPUSampler *sampler(SDL_GPUFilter filter,SDL_GPUSamplerAddressMode address) {
    SDL_GPUSamplerCreateInfo info={};
    info.min_filter=info.mag_filter=filter;info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u=info.address_mode_v=info.address_mode_w=address;
    return SDL_CreateGPUSampler(device,&info);
}
} // namespace gpu3d

using namespace gpu3d;
GpuTexture::~GpuTexture() {if(texture&&device)SDL_ReleaseGPUTexture(device,texture);}
GpuTextureRef gpuCreateTexture(GpuFormat format,int w,int h,const void *pixels,int bytesPerRow,int levels) {
    SDL_GPUTextureFormat textureFormat=format==GpuFormat::R8?SDL_GPU_TEXTUREFORMAT_R8_UNORM:format==GpuFormat::RG8?SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
        format==GpuFormat::RGBA8?SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
    int pixelBytes=format==GpuFormat::R8?1:format==GpuFormat::RG8?2:format==GpuFormat::RGBA8?4:8;
    if(levels<=1) {
        SDL_GPUTexture *texture=createTexture(textureFormat,w,h,SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if(!texture)return nullptr;
        upload(texture,w,h,1,pixels,(Uint32)(bytesPerRow*h),(Uint32)(bytesPerRow/pixelBytes));
        return GpuTextureRef(new GpuTexture{texture});
    }
    SDL_GPUTextureCreateInfo info={};
    info.type=SDL_GPU_TEXTURETYPE_2D;info.format=textureFormat;info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width=(Uint32)w;info.height=(Uint32)h;info.layer_count_or_depth=1;info.num_levels=(Uint32)levels;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture=SDL_CreateGPUTexture(device,&info);
    if(!texture)return nullptr;
    Uint32 bytes=0;
    for(int level=0;level<levels;++level)bytes+=(Uint32)(std::max(1,w>>level)*std::max(1,h>>level)*pixelBytes);
    SDL_GPUTransferBufferCreateInfo transferInfo={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes,0};
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(device,&transferInfo);
    if(!transfer) {SDL_ReleaseGPUTexture(device,texture);return nullptr;}
    memcpy(SDL_MapGPUTransferBuffer(device,transfer,false),pixels,bytes);
    SDL_UnmapGPUTransferBuffer(device,transfer);
    if(!uploadCommand)uploadCommand=SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(uploadCommand);
    Uint32 offset=0;
    for(int level=0;level<levels;++level) {
        Uint32 lw=(Uint32)std::max(1,w>>level),lh=(Uint32)std::max(1,h>>level);
        SDL_GPUTextureTransferInfo source={transfer,offset,lw,lh};
        SDL_GPUTextureRegion target={};
        target.texture=texture;target.mip_level=(Uint32)level;target.w=lw;target.h=lh;target.d=1;
        SDL_UploadToGPUTexture(copy,&source,&target,false);
        offset+=lw*lh*(Uint32)pixelBytes;
    }
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device,transfer);
    return GpuTextureRef(new GpuTexture{texture});
}
void gpuUpdateTexture(const GpuTextureRef &texture,GpuFormat format,const std::vector<std::array<int,4>> &rects,const void *pixels,int bytesPerRow) {
    if(!texture||rects.empty())return;
    int pixelBytes=format==GpuFormat::R8?1:format==GpuFormat::RG8?2:format==GpuFormat::RGBA8?4:8;
    Uint32 bytes=0;
    for(const auto &r:rects)bytes+=(Uint32)(r[2]*r[3]*pixelBytes);
    if(!bytes)return;
    SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes,0};
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(device,&info);
    if(!transfer) {fprintf(stderr,"GPU upload failed: %s\n",SDL_GetError());return;}
    byte *mapped=(byte*)SDL_MapGPUTransferBuffer(device,transfer,false);
    Uint32 offset=0;
    for(const auto &r:rects)for(int row=0;row<r[3];++row) {
        memcpy(mapped+offset,(const byte*)pixels+(size_t)(r[1]+row)*bytesPerRow+(size_t)r[0]*pixelBytes,(size_t)r[2]*pixelBytes);
        offset+=(Uint32)(r[2]*pixelBytes);
    }
    SDL_UnmapGPUTransferBuffer(device,transfer);
    if(!uploadCommand)uploadCommand=SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(uploadCommand);
    offset=0;
    for(const auto &r:rects) {
        if(r[2]<=0||r[3]<=0)continue;
        SDL_GPUTextureTransferInfo source={transfer,offset,(Uint32)r[2],(Uint32)r[3]};
        SDL_GPUTextureRegion target={};
        target.texture=texture->texture;target.x=(Uint32)r[0];target.y=(Uint32)r[1];target.w=(Uint32)r[2];target.h=(Uint32)r[3];target.d=1;
        // No cycling: the rest of the texture must keep its contents.
        SDL_UploadToGPUTexture(copy,&source,&target,false);
        offset+=(Uint32)(r[2]*r[3]*pixelBytes);
    }
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device,transfer);
}
int gpuMaxSamples() {return maxSamples;}
bool platformReadMask(const char *path,int w,int h,std::vector<float> &values) {
    SDL_Surface *loaded=SDL_LoadPNG(path);
    SDL_Surface *surface=loaded?SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_RGBA32):nullptr;
    SDL_DestroySurface(loaded);
    bool valid=surface&&surface->w==w&&surface->h==h;
    if(valid) {
        values.resize((size_t)w*h);
        for(int y=0;y<h;++y) {
            const byte *row=(const byte*)surface->pixels+(size_t)y*surface->pitch;
            for(int x=0;x<w;++x) {
                const byte *p=row+x*4;
                values[(size_t)y*w+x]=(p[0]+p[1]+p[2])/(3.0f*255.0f)*(p[3]/255.0f);
            }
        }
    }
    SDL_DestroySurface(surface);
    return valid;
}

// Mailbox presents the newest completed frame at refresh without tearing,
// instead of queuing old frames behind a missed FIFO refresh. Fall back to
// ordinary vsync on drivers without mailbox support.
static void configurePresentMode(SDL_Window *window,bool profiling) {
    SDL_GPUPresentMode mode=SDL_GPU_PRESENTMODE_VSYNC;
    if(profiling&&SDL_WindowSupportsGPUPresentMode(device,window,SDL_GPU_PRESENTMODE_IMMEDIATE))mode=SDL_GPU_PRESENTMODE_IMMEDIATE;
    else if(!M_CheckParm((char*)"-fifo-present")&&SDL_WindowSupportsGPUPresentMode(device,window,SDL_GPU_PRESENTMODE_MAILBOX))mode=SDL_GPU_PRESENTMODE_MAILBOX;
    if(!SDL_SetGPUSwapchainParameters(device,window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode)&&mode!=SDL_GPU_PRESENTMODE_VSYNC) {
        mode=SDL_GPU_PRESENTMODE_VSYNC;SDL_SetGPUSwapchainParameters(device,window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode);
    }
    fprintf(stderr,"GPU presentation: %s.\n",mode==SDL_GPU_PRESENTMODE_MAILBOX?"mailbox (tear-free)":mode==SDL_GPU_PRESENTMODE_IMMEDIATE?"immediate (profiler)":"FIFO vsync");
}

int I_Render3DInit(SDL_Window *window) {
    bool debug=M_CheckParm((char*)"-gpudebug")!=0;
    // SDL picks Metal on macOS and Vulkan elsewhere; SDL_GPU_DRIVER overrides it.
    device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL,debug,nullptr);
    if(!device) {fprintf(stderr,"GPU renderer unavailable: %s\n",SDL_GetError());return 0;}
    shaderFormat=(SDL_GetGPUShaderFormats(device)&SDL_GPU_SHADERFORMAT_SPIRV)?SDL_GPU_SHADERFORMAT_SPIRV:SDL_GPU_SHADERFORMAT_MSL;
    if(!SDL_ClaimWindowForGPUDevice(device,window)) {
        fprintf(stderr,"GPU renderer: %s\n",SDL_GetError());
        SDL_DestroyGPUDevice(device);device=nullptr;return 0;
    }
    configurePresentMode(window,false);
    swapchainFormat=SDL_GetGPUSwapchainTextureFormat(device,window);
    const auto sampled=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    depthFormat=SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTURETYPE_2D,sampled)?
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT:SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    for(auto [count,samples]:{std::pair{SDL_GPU_SAMPLECOUNT_8,8},{SDL_GPU_SAMPLECOUNT_4,4},{SDL_GPU_SAMPLECOUNT_2,2}})
        if(SDL_GPUTextureSupportsSampleCount(device,SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,count)&&
           SDL_GPUTextureSupportsSampleCount(device,depthFormat,count)) {maxSamples=samples;break;}
    // -gpumsaa 1 keeps the single-sample path whatever the setting says.
    int msaa=M_CheckParm((char*)"-gpumsaa");
    forceSingleSample=msaa&&msaa+1<myargc&&atoi(myargv[msaa+1])==1;
    linearRepeat=sampler(SDL_GPU_FILTER_LINEAR,SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
    linearClamp=sampler(SDL_GPU_FILTER_LINEAR,SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);
    nearestClamp=sampler(SDL_GPU_FILTER_NEAREST,SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);
    if(!linearRepeat||!linearClamp||!nearestClamp||!createPipelines()) {I_Render3DShutdown();return 0;}
    hudTexture=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,SCREENWIDTH,SCREENHEIGHT*2,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    settingsTexture=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,DOOM_SETTINGS_WIDTH,DOOM_SETTINGS_HEIGHT,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    paletteTexture=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,256,1,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    dummySeam=createTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,1,1,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    dummyContact=createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,1,1,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    dummyBake=createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,1,1,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    dummyRelief=createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,1,1,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    solidTexel=createTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,1,1,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if(!settingsTexture||!hudTexture||!paletteTexture||!dummySeam||!dummyContact||!dummyBake||!dummyRelief||!solidTexel) {I_Render3DShutdown();return 0;}
    std::array<byte,8> zero={};const byte covered[4]={0,255,0,255}; // Index 0, full coverage, no emission.
    const byte level[4]={128,128,0,255}; // No slope, no grain.
    upload(dummySeam,1,1,1,zero.data(),8,1);upload(dummyContact,1,1,1,zero.data(),4,1);upload(dummyBake,1,1,1,zero.data(),4,1);upload(dummyRelief,1,1,1,level,4,1);
    upload(solidTexel,1,1,1,covered,4,1);
    buildCloudTexture();
    flushUploads();
    sceneInit(window);
    applySettings();
    applySampleCount();
    fprintf(stderr,"GPU renderer: %s; up to %dx MSAA, native pixels, widescreen and depth-tested 3D.\n",
        SDL_GetGPUDeviceDriver(device),maxSamples);
    return 1;
}
void I_Render3DShutdown(void) {
    if(device)SDL_WaitForGPUIdle(device);
    sceneShutdown();
    flushUploads();
    releaseTargets();releaseReflection();width=height=0;
    for(SDL_GPUTexture **t:{&settingsTexture,&hudTexture,&paletteTexture,&paletteLUT,&dummySeam,&dummyContact,&dummyBake,&dummyRelief,&solidTexel})release(*t);
    releasePipelines();
    for(SDL_GPUSampler **s:{&linearRepeat,&linearClamp,&nearestClamp})release(*s,SDL_ReleaseGPUSampler);
    releaseFrameUploads();releaseLoading();
    if(device) {
        if(gameWindow)SDL_ReleaseWindowFromGPUDevice(device,gameWindow);
        SDL_DestroyGPUDevice(device);
    }
    device=nullptr;
}
void I_Render3DProfile(int enabled,unsigned skip) {
    profile.skip=enabled?skip:0;
    if(profile.enabled==(enabled!=0)||!device)return;
    profile.enabled=enabled!=0;
    configurePresentMode(gameWindow,profile.enabled);
}
