/* Public standalone host. Native game code and assets must be imported locally
 * from the user's supported original installation. No game data is bundled. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_snapshot_alloc.h"
static unsigned active_step;

static LONG CALLBACK fault(EXCEPTION_POINTERS *e) {
#ifdef UPPER_P2P
    /* The WebRTC C API catches C++ exceptions internally. Let its handlers
       reject invalid SDP instead of treating a handled C++ throw as a native
       game crash. Memory and CPU faults still follow the diagnostic below. */
    if(e->ExceptionRecord->ExceptionCode==0x20474343u||e->ExceptionRecord->ExceptionCode==0x21474343u||e->ExceptionRecord->ExceptionCode==0xe06d7363u)
        return EXCEPTION_CONTINUE_SEARCH;
#endif
    fprintf(stderr,"PROBE_FAULT step=%u code=%08lx rip=%llx",active_step,e->ExceptionRecord->ExceptionCode,
        (unsigned long long)e->ContextRecord->Rip);
    if(e->ExceptionRecord->NumberParameters>=2)
        fprintf(stderr," access=%llu address=%llx",(unsigned long long)e->ExceptionRecord->ExceptionInformation[0],
            (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
    fprintf(stderr,"\n");
#ifdef TRACE_NATIVE_FAULTS
    MEMORY_BASIC_INFORMATION stack_region;
    uintptr_t sp=(uintptr_t)e->ContextRecord->Rsp;
    if(VirtualQuery((void *)sp,&stack_region,sizeof(stack_region))){
        uintptr_t end=(uintptr_t)stack_region.BaseAddress+stack_region.RegionSize;
        if(end>sp+0x2000)end=sp+0x2000;
        for(uintptr_t p=sp;p+8<=end;p+=8){
            uintptr_t v=*(uintptr_t *)p;
            if(v>=UINT64_C(0x140001000)&&v<UINT64_C(0x140fdc000))
                fprintf(stderr,"stack_candidate +%llx = %llx\n",(unsigned long long)(p-sp),(unsigned long long)v);
        }
    }
#endif
    fflush(stderr);ExitProcess(20);
    return EXCEPTION_CONTINUE_SEARCH;
}
static size_t load_file(const char *name,void *buffer,size_t capacity) {
    FILE *f=fopen(name,"rb");if(!f){perror(name);ExitProcess(2);}
    size_t n=fread(buffer,1,capacity,f);
    if(ferror(f)||fgetc(f)!=EOF){fclose(f);ExitProcess(3);}fclose(f);return n;
}
static void *map(uintptr_t address,size_t length) {
    void *p=snap_allocate(address,length);
    if(p!=(void *)address){fprintf(stderr,"MAP_FAILED %llx err=%lu\n",(unsigned long long)address,GetLastError());ExitProcess(4);}
    return p;
}
#ifdef CAPTURE_TEXTURES
#include "texture_capture.h"
#endif
#ifdef UPPER_WINDOW
#include "native_window.h"
#endif
int main(int argc,char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#ifdef UPPER_WINDOW
    host_self_test=argc>1&&strcmp(argv[1],"--self-test")==0;
    if(!online_parse((unsigned)argc,argv)){MessageBoxW(NULL,L"Parametros de conexao invalidos.",L"Alpha 3 Upper Online",MB_OK|MB_ICONERROR);return 52;}
    if(host_self_test&&argc>2){unsigned n=(unsigned)strtoul(argv[2],NULL,10);if(n>=1&&n<=36000)host_test_frames=n;}
    if(host_self_test&&argc>3)host_test_direction=(unsigned)strtoul(argv[3],NULL,0);
    char executable[MAX_PATH];GetModuleFileNameA(NULL,executable,MAX_PATH);
    char *slash=strrchr(executable,'\\');if(slash){*slash=0;SetCurrentDirectoryA(executable);}
    freopen("data/run.log","w",stdout);freopen("data/error.log","w",stderr);
    char *defaults[]={"Alpha3Upper-Experimental.exe","data/main.bin","data/arena.bin","data/after-state.bin",NULL};
    argc=4;argv=defaults;
    options_load();
    if(online_active()){option_versus=1;option_training=0;}
#endif
    if(argc<4 || argc>6){fprintf(stderr,"Research only: image.bin arena.bin after-state.bin [tick|frame] [steps]\n");return 1;}
    AddVectoredExceptionHandler(1,fault);
    void *image=map(UINT64_C(0x140000000),0x4000000);
    size_t image_size=load_file(argv[1],image,0x4000000);
    void *arena_mapping=map(UINT64_C(0x08230000),0x10010000);
    unsigned char *arena=(unsigned char *)arena_mapping+0x1000;
    size_t arena_loaded=load_file(argv[2],arena,0x10000000);
#ifdef UPPER_COLD_BOOT
    /* VirtualAlloc supplies clean zeroed runtime RAM. Only the original AFS
     * resource archive is needed here, rather than an in-progress RAM image. */
    if(arena_loaded<8||memcmp(arena,"AFS\0",4)!=0)return 5;
#else
    if(arena_loaded!=0x10000000)return 5;
#endif
    DWORD old;
    if(!VirtualProtect((unsigned char *)image+0x1000,0xfdb000,PAGE_EXECUTE_READ,&old))return 6;
    FlushInstructionCache(GetCurrentProcess(),image,image_size);
    unsigned char *state=*(unsigned char **)(uintptr_t)UINT64_C(0x142c44050);
#ifndef UPPER_COLD_BOOT
    if(state<arena||state+0x20000>arena+0x10000000)return 7;
#endif
    /* Replace only the host frontend's packet-storage object, not game logic.
     * Original bridge functions append video/audio commands to these buffers.
     * No Direct3D device or audio output is created by this diagnostic. */
    unsigned char *frontend=snap_allocate(0,0xe0000);
    unsigned char *packets=snap_allocate(0,0x200000);
    unsigned char *packet_back=snap_allocate(0,0x200000);
    unsigned char *scratch=snap_allocate(0,0x10000);
    if(!frontend||!packets||!packet_back||!scratch)return 9;
    *(void **)(frontend+0xded18)=packets;
    *(void **)(frontend+0xded20)=packet_back;
    *(void **)(frontend+0xded28)=scratch;
    *(void **)(frontend+0xded30)=packets;
    *(void **)(uintptr_t)UINT64_C(0x141c14f90)=frontend;
    /* A separate host-side system-event queue is also outside the game arena.
     * The original append routine at 14006d470 records four integer fields. */
    unsigned char *system_queue=snap_allocate(0,0x35000);
    if(!system_queue)return 12;
    *(void **)(uintptr_t)UINT64_C(0x1422c82c8)=system_queue;
#ifdef CAPTURE_TEXTURES
    host_install_uploads();
#endif
#ifdef UPPER_WINDOW
#ifdef UPPER_COLD_BOOT
    /* Reinitialize the original game core, using only the resident resources.
     * No captured match state or preloaded texture set is used after this. */
    /* Collection-side allocation PRNG is otherwise inherited from the import
     * session and changes native state addresses. Start it identically on every
     * PC; the gameplay seed is still negotiated separately by netplay. */
    static const uint32_t public_boot_seed[4]={0x12345678u,0x9e3779b9u,0x243f6a88u,0xb7e15162u};
    memcpy((void *)(uintptr_t)UINT64_C(0x14221ed40),public_boot_seed,sizeof(public_boot_seed));
    unsigned char *boot_context=*(unsigned char **)(uintptr_t)UINT64_C(0x141c14f98);
    *(void **)boot_context=arena;
    options_apply_mode(boot_context);
    ((void (*)(void))(uintptr_t)UINT64_C(0x140dcb360))();
    state=*(unsigned char **)(uintptr_t)UINT64_C(0x142c44050);
    if(state<arena||state+0x20000>arena+0x10000000)return 7;
#endif
    return host_run_window(frontend,packets,system_queue,state);
#endif
    unsigned before=state[0x50];
    int full_frame=argc>=5 && strcmp(argv[4],"frame")==0;
    unsigned steps=argc==6?(unsigned)strtoul(argv[5],NULL,10):1;
    if(steps<1||steps>600)return 11;
    uintptr_t entry=argc>=5 && strcmp(argv[4],"tick")==0?UINT64_C(0x140dcc3f0):UINT64_C(0x140dcd5b0);
    unsigned char *context=*(unsigned char **)(uintptr_t)UINT64_C(0x141c14f98);
    if(full_frame)entry=*(uintptr_t *)(context+0x10);
    unsigned counter_before=*(unsigned *)(uintptr_t)UINT64_C(0x142c61984);
    printf("Calling original native entry=%llx; no stubs. state=%p tick_byte=%u\n",(unsigned long long)entry,(void *)state,before);
    fflush(stdout);
    unsigned long long total_packets=0;
    for(unsigned step=0;step<steps;step++) {
        active_step=step;
        *(unsigned *)(system_queue+0x34ab4)=0;
        *(void **)(frontend+0xded30)=packets;
        *(unsigned *)(frontend+0xded38)=0;
        *(unsigned *)(frontend+0xded3c)=0;
        ((void (*)(void))entry)();
        if(full_frame) ((void (*)(void))(*(uintptr_t *)(context+0x18)))();
        total_packets+=*(unsigned *)(frontend+0xded3c);
    }
    printf("Native steps completed=%u total_packets=%llu\n",steps,total_packets);
    printf("RETURNED original task callbacks; tick_byte=%u -> %u\n",before,(unsigned)state[0x50]);
    printf("Original frame counter=%u -> %u\n",counter_before,*(unsigned *)(uintptr_t)UINT64_C(0x142c61984));
    printf("Frontend packets=%u bytes=%u (not rendered)\n",*(unsigned *)(frontend+0xded3c),*(unsigned *)(frontend+0xded38));
    FILE *f=fopen(argv[3],"wb");if(!f)return 8;
    fwrite(state,1,0x20000,f);fclose(f);
    char packet_path[4096];snprintf(packet_path,sizeof(packet_path),"%s.packets",argv[3]);
    f=fopen(packet_path,"wb");if(f){fwrite(packets,1,*(unsigned *)(frontend+0xded38),f);fclose(f);}
#ifdef CAPTURE_TEXTURES
    f=fopen("work/texture-capture/palette-bank.raw","wb");if(f){fwrite(frontend+0xd8d14,1,0x2000,f);fclose(f);}
    printf("Texture uploads=%u palette uploads=%u\n",captured_textures,captured_palettes);
#endif
    VirtualFree(image,0,MEM_RELEASE);VirtualFree(arena_mapping,0,MEM_RELEASE);
    return 0;
}
