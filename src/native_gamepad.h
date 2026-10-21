/* XInput-compatible controller input is sampled once per game tick. Chords override their component bindings. */
#include <xinput.h>
#define PAD_LT 0x10000u
#define PAD_RT 0x20000u
#define PAD_ACTIONS 16
static const wchar_t *pad_names[PAD_ACTIONS]={L"Soco fraco",L"Soco m\u00e9dio",L"Soco forte",L"Chute fraco",L"Chute m\u00e9dio",L"Chute forte",L"3x socos",L"3x chutes",L"Soco + chute fracos",L"Soco + chute m\u00e9dios",L"Soco + chute fortes",L"Cr\u00e9dito",L"Start",L"Pausa",L"2x socos (agarr\u00e3o)",L"2x chutes (agarr\u00e3o)"};
static const unsigned pad_defaults[PAD_ACTIONS]={0x4000,0x8000,0x200,0x1000,0x2000,PAD_RT,0x100,PAD_LT,0x40,0x80,0x10100,0x20,0x10,0x30,0,0};
static const unsigned pad_outputs[PAD_ACTIONS]={0x8000,0x1000,0x200,0x4000,0x2000,0x800,0x9200,0x6800,0xc000,0x3000,0xa00,1,8,0,0x1200,0x2800};
typedef struct {unsigned bindings[PAD_ACTIONS],buttons,directions,slot,device;int connected;ULONGLONG retry;} PadPlayer;
static PadPlayer pads[2];
#define pad_bindings pads[0].bindings
#define pad_buttons pads[0].buttons
#define pad_directions pads[0].directions
#define pad_connected pads[0].connected
#define pad_slot pads[0].slot
static unsigned pad_edit_player;
static int pad_capture=-1,pad_capture_armed;
static unsigned pad_capture_bits;
static HWND pad_window,pad_status,pad_map_buttons[PAD_ACTIONS],pad_player_selector,pad_device_selector;
static DWORD (WINAPI *pad_get_state)(DWORD,XINPUT_STATE*);
static const char *pad_section(unsigned player){return player?"ControleXboxP2":"ControleXbox";}
static void pad_save(void){for(unsigned p=0;p<2;p++){
    for(unsigned i=0;i<PAD_ACTIONS;i++){char key[24],value[24];snprintf(key,sizeof(key),"Acao%u",i);snprintf(value,sizeof(value),"%u",pads[p].bindings[i]);WritePrivateProfileStringA(pad_section(p),key,value,option_file);}
    char value[12];snprintf(value,sizeof(value),"%u",pads[p].device);WritePrivateProfileStringA(pad_section(p),"Dispositivo",value,option_file);
}}
static void pad_init(void){
    const wchar_t *dlls[]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"};
    for(unsigned i=0;i<3&&!pad_get_state;i++){HMODULE m=LoadLibraryW(dlls[i]);if(m)pad_get_state=(void*)GetProcAddress(m,"XInputGetState");}
    for(unsigned p=0;p<2;p++){
        pads[p].slot=p;pads[p].connected=0;pads[p].retry=0;
        pads[p].device=GetPrivateProfileIntA(pad_section(p),"Dispositivo",4,option_file);if(pads[p].device>5)pads[p].device=4;
        for(unsigned i=0;i<PAD_ACTIONS;i++){char key[24];snprintf(key,sizeof(key),"Acao%u",i);unsigned v=GetPrivateProfileIntA(pad_section(p),key,pad_defaults[i],option_file);pads[p].bindings[i]=(v&~0x3f3f0u)?pad_defaults[i]:v;}
    }
}
/* Explicit remapping transfers an identical binding within this profile.
   Existing saved profiles are kept intact when loaded. */
static void pad_assign(unsigned player,unsigned action,unsigned bits){
    if(player>=2||action>=PAD_ACTIONS)return;
    for(unsigned i=0;i<PAD_ACTIONS;i++)if(i!=action&&bits&&pads[player].bindings[i]==bits)pads[player].bindings[i]=0;
    pads[player].bindings[action]=bits;pad_save();
}
static unsigned pad_actions_player(unsigned player,unsigned buttons){
    unsigned *bindings=pads[player].bindings;
    unsigned active=0;
    for(unsigned i=0;i<PAD_ACTIONS;i++)if(bindings[i]&&(buttons&bindings[i])==bindings[i]){
        int overridden=0;for(unsigned j=0;j<PAD_ACTIONS;j++)if(i!=j&&bindings[j]!=bindings[i]&&(bindings[j]&bindings[i])==bindings[i]&&(buttons&bindings[j])==bindings[j])overridden=1;
        if(!overridden)active|=1u<<i;
    }return active;
}
static unsigned pad_game_bits_player(unsigned player,unsigned buttons){unsigned active=pad_actions_player(player,buttons),bits=0;for(unsigned i=0;i<PAD_ACTIONS;i++)if(active&(1u<<i))bits|=pad_outputs[i];return bits;}
static unsigned pad_actions(unsigned buttons){return pad_actions_player(0,buttons);}
static unsigned pad_game_bits(unsigned buttons){return pad_game_bits_player(0,buttons);}
static void pad_label(unsigned bits,wchar_t *text,size_t n){
    const unsigned masks[]={0x1000,0x2000,0x4000,0x8000,0x100,0x200,PAD_LT,PAD_RT,0x40,0x80,0x20,0x10};
    const wchar_t *names[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"LT",L"RT",L"L3",L"R3",L"Back",L"Start"};
    text[0]=0;for(unsigned i=0;i<12;i++)if(bits&masks[i]){if(text[0])wcsncat(text,L" + ",n-wcslen(text)-1);wcsncat(text,names[i],n-wcslen(text)-1);}if(!text[0])wcsncpy(text,L"Sem atalho",n);
}
static void pad_refresh(void){for(unsigned i=0;i<PAD_ACTIONS;i++){wchar_t t[128];pad_label(pads[pad_edit_player].bindings[i],t,128);SetWindowTextW(pad_map_buttons[i],t);}}
static void pad_refresh_device(void){SendMessageW(pad_device_selector,CB_SETCURSEL,pads[pad_edit_player].device,0);}
static void pad_choose_device(unsigned player,unsigned device){
    if(device>5)return;pads[player].device=device;pads[player].connected=0;pads[player].buttons=pads[player].directions=0;pads[player].retry=0;
    if(device<4&&pads[1-player].device==device){pads[1-player].device=5;pads[1-player].connected=0;pads[1-player].buttons=pads[1-player].directions=0;}pad_save();
}
static LRESULT CALLBACK pad_proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_COMMAND){unsigned id=LOWORD(wp);
        if(id==300&&HIWORD(wp)==CBN_SELCHANGE){pad_edit_player=(unsigned)SendMessageW(pad_player_selector,CB_GETCURSEL,0,0);pad_capture=-1;pad_refresh();pad_refresh_device();SetWindowTextW(pad_status,L"Perfil do jogador selecionado. Escolha o controle e clique numa ação para remapear.");return 0;}
        if(id==301&&HIWORD(wp)==CBN_SELCHANGE){pad_choose_device(pad_edit_player,(unsigned)SendMessageW(pad_device_selector,CB_GETCURSEL,0,0));pad_capture=-1;SetWindowTextW(pad_status,L"Controle salvo. Automático separa os dois jogadores; um controle não comanda ambos.");return 0;}if(id>=100&&id<100+PAD_ACTIONS){pad_capture=id-100;pad_capture_bits=0;pad_capture_armed=0;SetWindowTextW(pad_status,L"Solte os bot\u00f5es. Pressione o bot\u00e3o ou combina\u00e7\u00e3o desejada e solte.");return 0;}
        if(id==200){memcpy(pads[pad_edit_player].bindings,pad_defaults,sizeof(pad_defaults));pad_save();pad_refresh();pad_capture=-1;SetWindowTextW(pad_status,L"Padr\u00e3o XInput restaurado.");return 0;}
        if(id==201&&pad_capture>=0){pads[pad_edit_player].bindings[pad_capture]=0;pad_save();pad_refresh();pad_capture=-1;SetWindowTextW(pad_status,L"Atalho removido.");return 0;}}
    if(m==WM_KEYDOWN&&wp==VK_ESCAPE){DestroyWindow(w);return 0;}
    if(m==WM_DESTROY){pad_window=NULL;pad_capture=-1;return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
static void pad_open(void){
    if(pad_window){SetForegroundWindow(pad_window);return;}
    WNDCLASSW wc={0};wc.lpfnWndProc=pad_proc;wc.hInstance=GetModuleHandle(NULL);wc.lpszClassName=L"UpperXboxOptions";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    RECT r={0,0,860,482};AdjustWindowRect(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE);
    pad_window=CreateWindowW(wc.lpszClassName,L"Controle XInput - bot\u00f5es e atalhos",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,host_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",L"Jogador:",WS_CHILD|WS_VISIBLE,16,16,110,24,pad_window,NULL,wc.hInstance,NULL);pad_player_selector=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,130,10,390,160,pad_window,(HMENU)300,wc.hInstance,NULL);SendMessageW(pad_player_selector,CB_ADDSTRING,0,(LPARAM)L"Jogador 1");SendMessageW(pad_player_selector,CB_ADDSTRING,0,(LPARAM)L"Jogador 2");SendMessageW(pad_player_selector,CB_SETCURSEL,pad_edit_player,0);
    CreateWindowW(L"STATIC",L"Controle:",WS_CHILD|WS_VISIBLE,16,56,110,24,pad_window,NULL,wc.hInstance,NULL);pad_device_selector=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,130,50,390,200,pad_window,(HMENU)301,wc.hInstance,NULL);const wchar_t *devices[]={L"XInput 1",L"XInput 2",L"XInput 3",L"XInput 4",L"Automático",L"Desativado (usar teclado)"};for(unsigned i=0;i<6;i++)SendMessageW(pad_device_selector,CB_ADDSTRING,0,(LPARAM)devices[i]);pad_refresh_device();
    for(unsigned i=0;i<PAD_ACTIONS;i++){int col=i/8,row=i%8,x=16+col*430;CreateWindowW(L"STATIC",pad_names[i],WS_CHILD|WS_VISIBLE,x,98+row*32,210,24,pad_window,NULL,wc.hInstance,NULL);pad_map_buttons[i]=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,x+214,92+row*32,190,28,pad_window,(HMENU)(UINT_PTR)(100+i),wc.hInstance,NULL);}
    pad_status=CreateWindowW(L"STATIC",L"Clique numa a\u00e7\u00e3o para mapear. Aceita combina\u00e7\u00f5es de bot\u00f5es.\nDirecional e anal\u00f3gico esquerdo movem o personagem.\nO jogo fica pausado enquanto esta janela est\u00e1 aberta.",WS_CHILD|WS_VISIBLE,16,362,828,66,pad_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"BUTTON",L"Restaurar padr\u00e3o XInput",WS_CHILD|WS_VISIBLE,16,442,400,30,pad_window,(HMENU)200,wc.hInstance,NULL);
    CreateWindowW(L"BUTTON",L"Remover atalho selecionado",WS_CHILD|WS_VISIBLE,446,442,398,30,pad_window,(HMENU)201,wc.hInstance,NULL);
    pad_refresh();if(!pads[pad_edit_player].connected)SetWindowTextW(pad_status,L"Controle XInput desconectado. Conecte-o para mapear.\nReconexão automática; botões e combinações são aceitos.\nO jogo fica pausado enquanto esta janela está aberta.");ShowWindow(pad_window,SW_SHOW);
}
static int pad_available(unsigned player,unsigned slot){unsigned other=1-player;return !(pads[other].device==slot||(pads[other].connected&&pads[other].slot==slot));}
static void pad_poll(void){
    int pause_edge=0;ULONGLONG now=GetTickCount64();
    for(unsigned player=0;player<2;player++){
        PadPlayer *p=&pads[player];unsigned before=p->buttons;int had=p->connected;p->buttons=p->directions=0;
        if(!pad_get_state||p->device==5){p->connected=0;continue;}if(!had&&now<p->retry)continue;
        XINPUT_STATE s={0};DWORD result=ERROR_DEVICE_NOT_CONNECTED;unsigned slot=p->device<4?p->device:p->slot;
        if(pad_available(player,slot))result=pad_get_state(slot,&s);
        if(result!=ERROR_SUCCESS&&p->device==4)for(unsigned i=0;i<4;i++)if(i!=slot&&pad_available(player,i)&&pad_get_state(i,&s)==ERROR_SUCCESS){slot=i;result=ERROR_SUCCESS;break;}
        if(result!=ERROR_SUCCESS){p->connected=0;p->retry=now+1000;continue;}p->slot=slot;p->connected=1;
        if(!had){printf("XINPUT_CONNECTED player=%u slot=%u\n",player+1,slot);fflush(stdout);if(pad_window&&pad_edit_player==player&&pad_capture<0)SetWindowTextW(pad_status,L"Controle conectado. Clique numa ação para remapear.");}
        unsigned b=s.Gamepad.wButtons;p->buttons=b&0xf3f0;if(s.Gamepad.bLeftTrigger>64)p->buttons|=PAD_LT;if(s.Gamepad.bRightTrigger>64)p->buttons|=PAD_RT;
        int x=s.Gamepad.sThumbLX,y=s.Gamepad.sThumbLY;if((b&1)||y>8000)p->directions|=0x10;if((b&2)||y<-8000)p->directions|=0x40;if((b&4)||x<-8000)p->directions|=0x80;if((b&8)||x>8000)p->directions|=0x20;
        if(pad_capture>=0&&pad_edit_player==player&&pad_window&&GetForegroundWindow()==pad_window){
            if(!pad_capture_armed){if(!p->buttons)pad_capture_armed=1;}else if(p->buttons)pad_capture_bits|=p->buttons;
            else if(pad_capture_bits){pad_assign(player,(unsigned)pad_capture,pad_capture_bits);pad_refresh();pad_capture=-1;SetWindowTextW(pad_status,L"Atalho salvo. Se o mesmo bot\u00e3o estava em outra a\u00e7\u00e3o, foi transferido.");}
        }
        if(had&&!pad_window&&!key_window&&GetForegroundWindow()==host_window){unsigned current=pad_actions_player(player,p->buttons),previous=pad_actions_player(player,before);if((current&(1u<<13))&&!(previous&(1u<<13)))pause_edge=1;}
    }
    if(pause_edge&&!online_active())host_paused=!host_paused;
}
