/* Host replacement for GPU upload endpoints only. The original CPU format
 * conversion routine is reused; no live process or installed file is patched. */
typedef struct { void *data; uint32_t row_pitch,depth_pitch; } HostMapped;
static unsigned captured_textures,captured_palettes;
#ifdef UPPER_WINDOW
typedef struct {unsigned w,h,bpp;unsigned char *pixels;} HostTexture;
static HostTexture host_textures[4096];
#endif
static void host_convert(void *frontend,unsigned fmt,void *dst,unsigned pitch,const void *src,unsigned w,unsigned h) {
    HostMapped m={dst,pitch,pitch*h};
    ((void (*)(void *,unsigned,HostMapped *,const void *,unsigned,unsigned,unsigned))(uintptr_t)UINT64_C(0x14004cac0))
        (frontend,fmt,&m,src,w,h,0);
}
static void host_texture_data(void *frontend,unsigned id,unsigned w,unsigned h,unsigned fmt,const void *src) {
    if(id>=4096||!src||!w||!h||w>4096||h>4096)ExitProcess(41);
    unsigned high=fmt&0xff00,low=fmt&255;
    unsigned bpp=((((high-0x500)&0xfffffcff)==0)||(low>=9&&low<=10))?1:4;
    unsigned char *pixels=calloc((size_t)w*h,bpp);if(!pixels)ExitProcess(42);
    host_convert(frontend,fmt,pixels,w*bpp,src,w,h);
#ifdef UPPER_WINDOW
#ifdef UPPER_ROLLBACK
    if(!rollback_texture_keep(id))free(host_textures[id].pixels);
#else
    free(host_textures[id].pixels);
#endif
    host_textures[id]=(HostTexture){w,h,bpp,pixels};captured_textures++;
#else
    char name[256];snprintf(name,sizeof(name),"work/texture-capture/tex-%04u.raw",id);
    FILE *f=fopen(name,"wb");if(!f)ExitProcess(43);
    uint32_t header[5]={id,w,h,fmt,bpp};fwrite(header,1,sizeof(header),f);
    fwrite(pixels,bpp,(size_t)w*h,f);fclose(f);free(pixels);captured_textures++;
#endif
}
static uint64_t host_texture_create(unsigned id,unsigned w,unsigned h,unsigned unused,unsigned fmt,const void *src) {
    (void)unused;
    void *frontend=*(void **)(uintptr_t)UINT64_C(0x141c14f90);
    host_texture_data(frontend,id,w,h,fmt,src);return 0;
}
static void host_palette_data(unsigned char *frontend,unsigned id,unsigned w,unsigned fmt,const void *src) {
    if(id>=256||!src||!w||w>256)ExitProcess(44);
    unsigned char pixels[1024]={0};
    host_convert(frontend,fmt,pixels,w*4,src,w,1);
#ifndef UPPER_WINDOW
    char name[256];snprintf(name,sizeof(name),"work/texture-capture/pal-%03u.raw",id);
    FILE *f=fopen(name,"wb");if(!f)ExitProcess(45);
    uint32_t header[3]={id,w,fmt};fwrite(header,1,sizeof(header),f);fwrite(pixels,4,w,f);fclose(f);
#endif
    if(id*64+w*4<=0x2000)memcpy(frontend+0xd8d14+id*64,pixels,w*4);
    captured_palettes++;
}
static void host_palette_init(unsigned count) {
    (void)count;
    unsigned char *frontend=*(unsigned char **)(uintptr_t)UINT64_C(0x141c14f90);
    /* The software renderer reads the CPU palette bank directly. */
    *(unsigned *)(frontend+0xded5c)=0;
    memset(frontend+0xd8d14,0,0x2000);
    memset(frontend+0xdad14,255,0x4000);
}
static void host_patch(uintptr_t address,void *target) {
    unsigned char patch[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    uint64_t pointer=(uint64_t)(uintptr_t)target;memcpy(patch+2,&pointer,8);
    DWORD old;VirtualProtect((void *)address,12,PAGE_EXECUTE_READWRITE,&old);
    memcpy((void *)address,patch,12);VirtualProtect((void *)address,12,old,&old);
    FlushInstructionCache(GetCurrentProcess(),(void *)address,12);
}
static void host_install_uploads(void) {
    unsigned char *host_frontend=*(unsigned char **)(uintptr_t)UINT64_C(0x141c14f90);
    host_frontend[0xded58]=1;
    /* Discard only cached sprite images; let original tile decoder rebuild
     * visible sprites from the captured game resources on the next update. */
    for(unsigned i=0;i<0x120;i++) {
        unsigned char *item=(unsigned char *)(uintptr_t)(UINT64_C(0x142c55510)+i*24);
        *(uint64_t *)item=0;item[0x12]=0;item[0x13]=0;
    }
    memset((void *)(uintptr_t)UINT64_C(0x142c57010),0,0x120*8);
    host_patch(UINT64_C(0x1400488e0),host_texture_create);
    host_patch(UINT64_C(0x14004e7e0),host_texture_data);
    host_patch(UINT64_C(0x14004e720),host_palette_data);
    host_patch(UINT64_C(0x140048d70),host_palette_init);
#ifndef UPPER_COLD_BOOT
    #include "texture_resources.h"
    for(unsigned i=0;i<sizeof(texture_resources)/sizeof(texture_resources[0]);i++) {
        const uint64_t *r=texture_resources[i];
        host_texture_create(r[0],r[1],r[2],0,r[3],(void *)(uintptr_t)r[5]);
    }
#endif
}
