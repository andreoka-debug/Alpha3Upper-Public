/* Experimental local frontend: software triangles and a Windows DIB window.
 * Game updates remain the original native callbacks in the private capture. */
#include <math.h>
#include "native_settings.h"
#ifdef UPPER_AUDIO
#include "native_audio.h"
#endif
#define SCREEN_W 384
#define SCREEN_H 224
static uint32_t screen_pixels[SCREEN_W*SCREEN_H];
static float screen_depth[SCREEN_W*SCREEN_H];
static HWND host_window,key_window;
static HMENU volume_effect_menu,volume_music_menu,difficulty_menu,rounds_menu;
static int host_quit,host_self_test,host_restart,host_paused;
static unsigned host_test_frames=180;
static unsigned host_test_direction=0x10;
static unsigned host_missing;
static int host_skip_raster;
static int online_active(void);
#include "native_gamepad.h"
#include "native_keyboard.h"
typedef struct {float x,y,z,u,v;uint32_t color,extra;} HostVertex;
static float edge(float ax,float ay,float bx,float by,float x,float y){return (x-ax)*(by-ay)-(y-ay)*(bx-ax);}
static int clampi(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static void triangle(const HostVertex *a,const HostVertex *b,const HostVertex *c,unsigned tid,unsigned pal,unsigned char *frontend){
    float ax=(a->x+1)*SCREEN_W*.5f,ay=(1-a->y)*SCREEN_H*.5f;
    float bx=(b->x+1)*SCREEN_W*.5f,by=(1-b->y)*SCREEN_H*.5f;
    float cx=(c->x+1)*SCREEN_W*.5f,cy=(1-c->y)*SCREEN_H*.5f;
    float den=edge(bx,by,cx,cy,ax,ay);if(fabsf(den)<1e-8f)return;
    int x0=clampi((int)floorf(fminf(ax,fminf(bx,cx))),0,SCREEN_W-1);
    int x1=clampi((int)ceilf(fmaxf(ax,fmaxf(bx,cx))),0,SCREEN_W-1);
    int y0=clampi((int)floorf(fminf(ay,fminf(by,cy))),0,SCREEN_H-1);
    int y1=clampi((int)ceilf(fmaxf(ay,fmaxf(by,cy))),0,SCREEN_H-1);
    HostTexture *tex=tid==0xffff?NULL:&host_textures[tid];
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        float wa=edge(bx,by,cx,cy,x+.5f,y+.5f)/den;
        float wb=edge(cx,cy,ax,ay,x+.5f,y+.5f)/den,wc=1-wa-wb;
        if(wa<-1e-6f||wb<-1e-6f||wc<-1e-6f)continue;
        unsigned pos=y*SCREEN_W+x;float z=wa*a->z+wb*b->z+wc*c->z;
        if(z>screen_depth[pos]+1e-7f)continue;
        float r=wa*((a->color>>16)&255)+wb*((b->color>>16)&255)+wc*((c->color>>16)&255);
        float g=wa*((a->color>>8)&255)+wb*((b->color>>8)&255)+wc*((c->color>>8)&255);
        float bl=wa*(a->color&255)+wb*(b->color&255)+wc*(c->color&255);
        float alpha=(wa*(a->color>>24)+wb*(b->color>>24)+wc*(c->color>>24))/255.f;
        if(tex){
            float u=wa*a->u+wb*b->u+wc*c->u,v=wa*a->v+wb*b->v+wc*c->v;
            unsigned tx=clampi((int)(u*tex->w),0,tex->w-1),ty=clampi((int)(v*tex->h),0,tex->h-1);
            unsigned char *s=tex->pixels+(ty*tex->w+tx)*tex->bpp;
            if(tex->bpp==1){unsigned pi=(pal?pal-1:0)*16+*s;pi=pi<2048?pi:2047;s=frontend+0xd8d14+pi*4;}
            r*=s[0]/255.f;g*=s[1]/255.f;bl*=s[2]/255.f;alpha*=s[3]/255.f;
        }
        if(alpha<=.001f)continue;
        uint32_t dst=screen_pixels[pos];float inv=1-alpha;
        unsigned rr=clampi((int)(r*alpha+((dst>>16)&255)*inv),0,255);
        unsigned gg=clampi((int)(g*alpha+((dst>>8)&255)*inv),0,255);
        unsigned bb=clampi((int)(bl*alpha+(dst&255)*inv),0,255);
        screen_pixels[pos]=(rr<<16)|(gg<<8)|bb;screen_depth[pos]=z;
    }
}
static void render_packets(unsigned char *frontend,unsigned char *packets){
    if(!host_skip_raster)for(unsigned i=0;i<SCREEN_W*SCREEN_H;i++){screen_pixels[i]=0;screen_depth[i]=2;}
    unsigned length=*(unsigned *)(frontend+0xded38);host_missing=0;
    for(unsigned off=0;off+24<=length;){
        uint32_t h[6];memcpy(h,packets+off,24);if(h[1]>length-off-24)break;
        if(h[2]==4&&h[1]==112){
            HostVertex v[4];memcpy(v,packets+off+24,112);unsigned tid=h[3]&65535,pal=h[3]>>16;
            if(tid!=65535&&(tid>=4096||!host_textures[tid].pixels)){host_missing++;}
            else if(!host_skip_raster){triangle(v,v+1,v+2,tid,pal,frontend);triangle(v+2,v+1,v+3,tid,pal,frontend);}
        }
        off+=24+h[1];
    }
}
static BITMAPINFO bitmap_info(void){BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=SCREEN_W;bi.bmiHeader.biHeight=-SCREEN_H;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;return bi;}
#include "native_training.h"
#include "native_versus.h"
#include "native_snapshot.h"
#include "native_performance.h"
#include "native_online.h"
#include "native_display.h"
static void update_volume_menu(void){
    CheckMenuRadioItem(volume_effect_menu,4000,4100,4000+(unsigned)option_effects,MF_BYCOMMAND);
    CheckMenuRadioItem(volume_music_menu,4200,4300,4200+(unsigned)option_music,MF_BYCOMMAND);
}
static void update_game_menu(void){
    CheckMenuRadioItem(difficulty_menu,4400,4408,4400+option_difficulty,MF_BYCOMMAND);
    CheckMenuRadioItem(rounds_menu,4501,4503,4500+option_rounds,MF_BYCOMMAND);
}
static void create_options_menu(void){
    HMENU bar=CreateMenu(),sound=CreatePopupMenu();volume_effect_menu=CreatePopupMenu();volume_music_menu=CreatePopupMenu();
    for(unsigned v=0;v<=100;v+=25){wchar_t label[16];swprintf(label,16,L"%u%%",v);AppendMenuW(volume_effect_menu,MF_STRING,4000+v,label);AppendMenuW(volume_music_menu,MF_STRING,4200+v,label);}
    AppendMenuW(sound,MF_POPUP,(UINT_PTR)volume_effect_menu,L"Efeitos e vozes");AppendMenuW(sound,MF_POPUP,(UINT_PTR)volume_music_menu,L"Música");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)sound,L"Som");
    HMENU game=CreatePopupMenu();difficulty_menu=CreatePopupMenu();rounds_menu=CreatePopupMenu();
    AppendMenuW(game,MF_STRING|(!option_training&&!option_versus?MF_CHECKED:0),4910,L"Iniciar arcade");
    AppendMenuW(game,MF_STRING|(option_versus?MF_CHECKED:0),4911,L"Iniciar versus local (P1 x P2)");
    if(option_versus)AppendMenuW(game,MF_STRING,4912,L"Versus: nova luta / selecionar personagens");
    AppendMenuW(game,MF_SEPARATOR,0,NULL);
    AppendMenuW(difficulty_menu,MF_STRING,4400,L"Automática");
    for(unsigned v=1;v<=8;v++){wchar_t label[64];swprintf(label,64,v==1?L"%u (mais fácil)":v==8?L"%u (mais difícil)":L"%u",v);AppendMenuW(difficulty_menu,MF_STRING,4400+v,label);}
    for(unsigned v=1;v<=3;v++){wchar_t label[32];swprintf(label,32,L"Vencer %u round(s)",v);AppendMenuW(rounds_menu,MF_STRING,4500+v,label);}
    AppendMenuW(game,MF_POPUP,(UINT_PTR)difficulty_menu,L"Dificuldade");
    AppendMenuW(game,MF_POPUP,(UINT_PTR)rounds_menu,L"Rounds para vencer");
    AppendMenuW(game,MF_STRING,4900,L"Configurar treino...");
    if(option_training){AppendMenuW(game,MF_STRING,4901,L"Reposicionar no treino");AppendMenuW(game,MF_STRING,4902,L"Parar grava\u00e7\u00e3o / reprodu\u00e7\u00e3o do dummy");}
    AppendMenuW(game,MF_SEPARATOR,0,NULL);
    AppendMenuW(game,MF_STRING|MF_GRAYED,0,L"Altera\u00e7\u00f5es de partida exigem rein\u00edcio");
    AppendMenuW(game,MF_STRING,4600,L"Reset / reiniciar e aplicar");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)game,L"Partida");
    display_add_menu(bar);HMENU controls=CreatePopupMenu();AppendMenuW(controls,MF_STRING,4800,L"Configurar controle XInput...");AppendMenuW(controls,MF_STRING,4801,L"Configurar teclado e Reset...");AppendMenuW(bar,MF_POPUP,(UINT_PTR)controls,L"Controle");
    performance_add_menu(bar);
    online_add_menu(bar);
    if(online_active()){
        EnableMenuItem(bar,1,MF_BYPOSITION|MF_GRAYED);
        EnableMenuItem(controls,4800,MF_BYCOMMAND|MF_GRAYED);EnableMenuItem(controls,4801,MF_BYCOMMAND|MF_GRAYED);
    }
    SetMenu(host_window,bar);update_volume_menu();update_game_menu();
}
static LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ENTERMENULOOP||msg==WM_EXITMENULOOP||msg==WM_ENTERSIZEMOVE||msg==WM_EXITSIZEMOVE||msg==WM_ACTIVATEAPP)performance_suspend();
    if(msg==WM_COMMAND){unsigned id=LOWORD(wp);
        if(id==5100){if(!online_active())online_open();return 0;}
#ifdef UPPER_ONLINE
        if(id==5103){if(!online_active())lobby_open();return 0;}
#endif
#ifdef UPPER_ONLINE
        if(id==5101){if(online_active()&&net_initialized)online_copy_invite();return 0;}
        if(id==5102){if(online_active()){online_launch_arguments[0]=0;host_restart=1;DestroyWindow(w);}return 0;}
#endif
        if(online_active()&&((id>=4400&&id<=4600)||(id>=4800&&id<=4912)))return 0;
        if(id>=5000&&id<=5003){performance_command(id);return 0;}
        if(id==4910||id==4911){options_save_mode(id==4911,0);host_restart=1;DestroyWindow(w);return 0;}
        if(id==4912){versus_new_selection();return 0;}
        if(id==4900){training_open();return 0;}
        if(id==4902){training_record_action(206);return 0;}
        if(id==4901){if(training_in_battle())training_reset_requested=1;return 0;}
        if(id==4800){pad_open();return 0;}
        if(id==4801){key_open();return 0;}
        if(id==4700){display_fullscreen(!option_fullscreen);return 0;}
        if(id==4710||id==4711){option_aspect=id-4710;options_save_display();display_update_menu();InvalidateRect(w,NULL,FALSE);return 0;}
        if(id>=4720&&id<=4724){option_crt=id-4720;options_save_display();display_update_menu();InvalidateRect(w,NULL,FALSE);return 0;}
        if(id>=4400&&id<=4408){options_save_game(0,id-4400);update_game_menu();return 0;}
        if(id>=4501&&id<=4503){options_save_game(1,id-4500);update_game_menu();return 0;}
        if(id==4600){host_restart=1;DestroyWindow(w);return 0;}if(id>=4000&&id<=4100){options_save_audio(0,id-4000);update_volume_menu();return 0;}if(id>=4200&&id<=4300){options_save_audio(1,id-4200);update_volume_menu();return 0;}}
    if((msg==WM_SYSKEYDOWN&&wp==VK_RETURN&&!(lp&(1L<<30)))||(msg==WM_KEYDOWN&&wp==VK_F11&&!(lp&(1L<<30)))){display_fullscreen(!option_fullscreen);return 0;}
    if((msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN)&&wp==VK_F10){if(fullscreen_active)SetMenu(w,GetMenu(w)?NULL:host_menu);DrawMenuBar(w);return 0;}
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}
    if(msg==WM_DESTROY){display_release();online_close_window();if(training_window)DestroyWindow(training_window);if(key_window)DestroyWindow(key_window);if(pad_window)DestroyWindow(pad_window);host_quit=1;PostQuitMessage(0);return 0;}
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){DestroyWindow(w);return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);paint_frame(dc);EndPaint(w,&ps);return 0;}
    if(msg==WM_ERASEBKGND)return 1;
    return DefWindowProcW(w,msg,wp,lp);
}
static unsigned keyboard_input_player(unsigned player){
    if(GetForegroundWindow()!=host_window)return 0;
    unsigned bits=pad_game_bits_player(player,pads[player].buttons)|pads[player].directions;
    unsigned char down[256];memcpy(down,key_down,sizeof(down));if(GetAsyncKeyState(VK_MENU)&0x8000)down[VK_RETURN]=0;
    bits|=key_game_bits_player(player,down);
    if((bits&0x50)==0x50)bits&=~0x50u;if((bits&0xa0)==0xa0)bits&=~0xa0u;return bits;
}
static void save_bitmap(const char *path){
    BITMAPINFO bi=bitmap_info();BITMAPFILEHEADER fh={0};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+sizeof(screen_pixels);
    FILE *f=fopen(path,"wb");if(f){fwrite(&fh,sizeof(fh),1,f);fwrite(&bi.bmiHeader,sizeof(BITMAPINFOHEADER),1,f);fwrite(screen_pixels,sizeof(screen_pixels),1,f);fclose(f);}
}
static int host_run_window(unsigned char *frontend,unsigned char *packets,unsigned char *system_queue,unsigned char *state){
    unsigned *input_script=NULL,*input_script_p2=NULL;size_t input_script_size=0,input_script_p2_size=0;
    FILE *events=host_self_test?fopen("data/events.csv","w"):NULL;
    if(events)fprintf(events,"frame,type,a,b,c\n");
    if(host_self_test){FILE *script=fopen("data/input-script.bin","rb");if(script){input_script=calloc(36000,4);input_script_size=fread(input_script,4,36000,script);fclose(script);}}
    if(host_self_test){FILE *script=fopen("data/input-script-p2.bin","rb");if(script){input_script_p2=calloc(36000,4);input_script_p2_size=fread(input_script_p2,4,36000,script);fclose(script);}}
    unsigned char *context=*(unsigned char **)(uintptr_t)UINT64_C(0x141c14f98);
#ifdef UPPER_AUDIO
    audio_init(host_self_test);
#endif
    training_context=context;training_state=state;
    if(option_training){*(int *)(context+0xca8)=0;*(int *)(context+0xcac)=0;}
    options_apply_game(context);frame_install();pad_init();key_init();
    uintptr_t tick=*(uintptr_t *)(context+0x10),draw=*(uintptr_t *)(context+0x18);
    HINSTANCE instance=GetModuleHandle(NULL);WNDCLASSW wc={0};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=L"UpperNativeResearch";wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&wc);
    RECT size={0,0,768,576};AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,TRUE);
#ifdef UPPER_ROLLBACK
    const wchar_t *window_title=L"Alpha 3 Upper Public v0.16.0 | Lobby e rollback | P2P direto";
#elif defined(UPPER_P2P)
    const wchar_t *window_title=L"Alpha 3 Upper v0.13.0 | Convite P2P direto | Partida: arcade / versus / treino";
#elif defined(UPPER_ONLINE)
    const wchar_t *window_title=L"Alpha 3 Upper v0.12.0 | Online experimental | Partida: arcade / versus / treino";
#else
    const wchar_t *window_title=L"Alpha 3 Upper v0.11.1 | Partida: arcade / versus / treino | Diagnostico: desempenho";
#endif
    host_window=CreateWindowExW(0,wc.lpszClassName,window_title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,NULL,NULL,instance,NULL);
    if(!host_window)return 51;
    HICON small_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
    if(small_icon)SendMessageW(host_window,WM_SETICON,ICON_SMALL,(LPARAM)small_icon);
    create_options_menu();
    if(!host_self_test){ShowWindow(host_window,SW_SHOW);if(option_fullscreen)display_fullscreen(1);}
#ifdef UPPER_ONLINE
    if(online_active()&&!net_open()){snprintf(net_error,sizeof(net_error),"Nao foi possivel iniciar a rede.");InterlockedExchange(&net_state,NET_FAILED);}
#endif
    LARGE_INTEGER freq,start,now;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);unsigned frames=0,paced_frames=0,peak_missing=0,battle_updates=0;
    int x_before=*(int *)(state+0x1f8+0x10),y_before=*(int *)(state+0x1f8+0x14);unsigned counter_before=*(unsigned *)(uintptr_t)UINT64_C(0x142c61984);
    MSG msg;while(!host_quit){
        while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){if(key_window&&key_capture>=0&&(msg.hwnd==key_window||IsChild(key_window,msg.hwnd))&&(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN||msg.message==WM_KEYUP||msg.message==WM_SYSKEYUP))continue;TranslateMessage(&msg);DispatchMessage(&msg);}
        if(host_quit)break;
        performance_check_error();
        performance_begin();
        if(!host_self_test){pad_poll();key_poll();}
        if(host_quit)break;
        if((host_paused||pad_window||key_window||training_window||online_config_open())&&!host_self_test){performance_suspend();Sleep(10);QueryPerformanceCounter(&start);paced_frames=0;continue;}
        active_step=frames;
        unsigned bits=host_self_test?(frames<90?host_test_direction:((frames%20)<10?0x8000:0)):keyboard_input_player(0);
        if(input_script)bits=frames<input_script_size?input_script[frames]:0;
        unsigned bits_p2=host_self_test?0:keyboard_input_player(1);if(input_script_p2)bits_p2=frames<input_script_p2_size?input_script_p2[frames]:0;
        performance_mark(1);
        if(online_active()&&online_step(&bits,&bits_p2,context,state)!=1){
            if(!host_self_test){HDC dc=GetDC(host_window);paint_frame(dc);ReleaseDC(host_window,dc);}Sleep(2);QueryPerformanceCounter(&start);paced_frames=0;continue;
        }
        training_before_tick(&bits,&bits_p2);
        versus_before_tick(&bits,&bits_p2);
        unsigned char previous_update=state[0x50];unsigned short previous_phase=*(unsigned short *)(state+0x2a);unsigned short previous_mode=*(unsigned short *)(state+0x28);
        *(unsigned *)(context+0x218)=bits;*(unsigned *)(context+0x21c)=bits_p2;*(unsigned *)(context+0x220)=0;
        *(unsigned *)(system_queue+0x34ab4)=0;*(void **)(frontend+0xded30)=packets;*(unsigned *)(frontend+0xded38)=0;*(unsigned *)(frontend+0xded3c)=0;
        host_skip_raster=host_self_test&&frames+1!=host_test_frames;
        ((void (*)(void))tick)();((void (*)(void))draw)();
#ifdef UPPER_ROLLBACK
        if(online_active()&&!rollback_after_tick())continue;
#endif
        performance_mark(2);
        render_packets(frontend,packets);training_after_tick(bits,bits_p2);performance_mark(3);
        if(previous_mode==1&&previous_phase==2&&*(unsigned short *)(state+0x28)==1&&*(unsigned short *)(state+0x2a)==2){unsigned delta=(unsigned char)(state[0x50]-previous_update);if(delta<=4)battle_updates+=delta;}
#ifdef UPPER_AUDIO
#ifdef UPPER_ROLLBACK
        if(online_active())rollback_audio_frame();else
#endif
        audio_frame(system_queue);
#endif
        if(events){unsigned count=*(unsigned *)(system_queue+0x34ab4);for(unsigned i=0;i<count&&i<64;i++){unsigned *e=(unsigned *)(system_queue+0x346b4+i*16);fprintf(events,"%u,%u,%u,%u,%u\n",frames,e[0],e[1],e[2],e[3]);}}
        if(host_missing>peak_missing)peak_missing=host_missing;
        performance_mark(4);
        if(!host_self_test){HDC dc=GetDC(host_window);paint_frame(dc);ReleaseDC(host_window,dc);}
        performance_finish(state);
#ifdef UPPER_TEST_TICK
        UPPER_TEST_TICK(state);
#endif
        frames++;
        if(host_self_test&&frames%1000==0){printf("FRAME_PROGRESS %u phase=%u/%u stage=%u\n",frames,*(unsigned short *)(state+0x28),*(unsigned short *)(state+0x2a),*(unsigned short *)(state+0x120));fflush(stdout);}
        if(host_self_test&&frames==host_test_frames)break;
        if(!host_self_test){paced_frames++;QueryPerformanceCounter(&now);double target=paced_frames/60.0,elapsed=(double)(now.QuadPart-start.QuadPart)/freq.QuadPart;if(elapsed<target)Sleep((DWORD)((target-elapsed)*1000));else if(elapsed-target>.1){start=now;paced_frames=0;}}
    }
    performance_shutdown();
    net_close();
    if(host_self_test){
        if(events)fclose(events);if(frame_trace){fclose(frame_trace);frame_trace=NULL;}
        save_bitmap("data/self-test.bmp");frame_dump();
        FILE *dump=fopen("data/last-frame.packets","wb");if(dump){fwrite(packets,1,*(unsigned *)(frontend+0xded38),dump);fclose(dump);}
        dump=fopen("data/last-context.bin","wb");if(dump){fwrite(context,1,0x4700,dump);fclose(dump);}
        dump=fopen("data/last-state.bin","wb");if(dump){fwrite(state,1,0x1b528,dump);fclose(dump);}
    }
#ifdef UPPER_AUDIO
    audio_shutdown();
#endif
    printf("WINDOW_RESULT frames=%u counter=%u->%u x=%d->%d y=%d->%d uploads=%u missing_peak=%u missing_last=%u battle_updates=%u\n",frames,counter_before,*(unsigned *)(uintptr_t)UINT64_C(0x142c61984),x_before,*(int *)(state+0x1f8+0x10),y_before,*(int *)(state+0x1f8+0x14),captured_textures,peak_missing,host_missing,battle_updates);fflush(stdout);
    if(host_restart){
        wchar_t exe[MAX_PATH];GetModuleFileNameW(NULL,exe,MAX_PATH);
        STARTUPINFOW si={0};si.cb=sizeof(si);PROCESS_INFORMATION pi={0};
        wchar_t command[2048];swprintf(command,2048,L"\"%ls\" %ls",exe,online_launch_arguments);
        if(CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
    }
    return 0;
}
