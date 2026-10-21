/* Persistent keyboard bindings. Host actions fire on a press edge, never on key repeat. */
#define KEY_ACTIONS 19
static const wchar_t *key_names[KEY_ACTIONS]={L"Cima",L"Baixo",L"Esquerda",L"Direita",L"Soco fraco",L"Soco m\u00e9dio",L"Soco forte",L"Chute fraco",L"Chute m\u00e9dio",L"Chute forte",L"3x socos",L"3x chutes",L"Soco + chute fracos",L"Soco + chute m\u00e9dios",L"Soco + chute fortes",L"Cr\u00e9dito",L"Start",L"Pausa",L"Reset / reiniciar"};
static const unsigned key_defaults[KEY_ACTIONS]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'S','D','F','X','C','V','U','I','J','K','L','1',VK_RETURN,'P',VK_F5};
static const unsigned key_outputs[KEY_ACTIONS]={0x10,0x40,0x80,0x20,0x8000,0x1000,0x200,0x4000,0x2000,0x800,0x9200,0x6800,0xc000,0x3000,0xa00,1,8,0,0};
static const unsigned key_defaults_p2[KEY_ACTIONS]={VK_NUMPAD8,VK_NUMPAD2,VK_NUMPAD4,VK_NUMPAD6,VK_NUMPAD7,VK_NUMPAD9,VK_MULTIPLY,VK_NUMPAD1,VK_NUMPAD3,VK_SUBTRACT,VK_NUMPAD0,VK_DECIMAL,VK_NUMPAD5,VK_ADD,VK_DIVIDE,'2',VK_SPACE,0,0};
static unsigned key_bindings[KEY_ACTIONS],key_bindings_p2[KEY_ACTIONS],key_edit_player;
static unsigned *key_map(unsigned player){return player?key_bindings_p2:key_bindings;}
static const char *key_section(unsigned player){return player?"TecladoP2":"Teclado";}
static unsigned char key_down[256],key_before[256];
static int key_hotkeys_armed,key_capture=-1,key_capture_armed;
static HWND key_status,key_buttons[KEY_ACTIONS],key_player_selector;
static int key_allowed(unsigned v){return v==0||(v>=8&&v<255&&v!=VK_ESCAPE&&v!=VK_F10&&v!=VK_F11&&v!=VK_MENU&&v!=VK_LMENU&&v!=VK_RMENU&&v!=VK_LWIN&&v!=VK_RWIN);}
static void key_save(void){for(unsigned p=0;p<2;p++)for(unsigned i=0;i<KEY_ACTIONS;i++){char name[24],value[16];snprintf(name,sizeof(name),"Acao%u",i);snprintf(value,sizeof(value),"%u",key_map(p)[i]);WritePrivateProfileStringA(key_section(p),name,value,option_file);}}
static void key_init(void){
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<KEY_ACTIONS;i++){char name[24];snprintf(name,sizeof(name),"Acao%u",i);unsigned def=(p?key_defaults_p2:key_defaults)[i];unsigned v=GetPrivateProfileIntA(key_section(p),name,def,option_file);key_map(p)[i]=key_allowed(v)?v:def;}
    /* Keep established P1 assignments if a new P2 default would collide. */
    for(unsigned i=0;i<KEY_ACTIONS;i++)for(unsigned j=0;j<KEY_ACTIONS;j++)if(key_bindings_p2[i]&&key_bindings_p2[i]==key_bindings[j])key_bindings_p2[i]=0;
    memset(key_down,0,sizeof(key_down));memset(key_before,0,sizeof(key_before));key_hotkeys_armed=0;
}
static void key_label(unsigned v,wchar_t *out,size_t n){
    if(!v){wcsncpy(out,L"Sem tecla",n);return;}UINT scan=MapVirtualKeyW(v,MAPVK_VK_TO_VSC);LONG code=(LONG)(scan<<16);
    if(v==VK_UP||v==VK_DOWN||v==VK_LEFT||v==VK_RIGHT||v==VK_INSERT||v==VK_DELETE||v==VK_HOME||v==VK_END||v==VK_PRIOR||v==VK_NEXT||v==VK_DIVIDE)code|=1<<24;
    if(!GetKeyNameTextW(code,out,(int)n))swprintf(out,n,L"Tecla %u",v);
}
static void key_refresh(void){for(unsigned i=0;i<KEY_ACTIONS;i++){wchar_t label[64];key_label(key_map(key_edit_player)[i],label,64);SetWindowTextW(key_buttons[i],label);}}
static void key_assign(unsigned action,unsigned v){
    if(action>=KEY_ACTIONS||!key_allowed(v))return;
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<KEY_ACTIONS;i++)if((p!=key_edit_player||i!=action)&&v&&key_map(p)[i]==v)key_map(p)[i]=0;
    key_map(key_edit_player)[action]=v;key_save();
}
static LRESULT CALLBACK key_proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_COMMAND){unsigned id=LOWORD(wp);
        if(id==300&&HIWORD(wp)==CBN_SELCHANGE){key_edit_player=(unsigned)SendMessageW(key_player_selector,CB_GETCURSEL,0,0);key_capture=-1;key_refresh();SetWindowTextW(key_status,key_edit_player?L"Jogador 2. Teclado numérico requer Num Lock ligado. Clique numa ação para remapear.":L"Jogador 1. Clique numa ação para remapear.");return 0;}
        if(id>=100&&id<100+KEY_ACTIONS){key_capture=id-100;key_capture_armed=0;SetWindowTextW(key_status,L"Solte as teclas e pressione a nova tecla. Esc cancela.\nUma tecla por a\u00e7\u00e3o; teclas repetidas s\u00e3o transferidas para a nova a\u00e7\u00e3o.");return 0;}
        if(id==200){memcpy(key_map(key_edit_player),key_edit_player?key_defaults_p2:key_defaults,sizeof(key_bindings));for(unsigned i=0;i<KEY_ACTIONS;i++)for(unsigned j=0;j<KEY_ACTIONS;j++)if(key_map(key_edit_player)[i]&&key_map(key_edit_player)[i]==key_map(1-key_edit_player)[j])key_map(1-key_edit_player)[j]=0;key_save();key_refresh();key_capture=-1;SetWindowTextW(key_status,L"Teclas do jogador selecionado restauradas.");return 0;}
        if(id==201&&key_capture>=0){key_assign(key_capture,0);key_refresh();key_capture=-1;SetWindowTextW(key_status,L"Tecla removida.");return 0;}
    }
    if(m==WM_DESTROY){key_window=NULL;key_capture=-1;return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
static void key_open(void){
    if(key_window){SetForegroundWindow(key_window);return;}
    WNDCLASSW wc={0};wc.lpfnWndProc=key_proc;wc.hInstance=GetModuleHandle(NULL);wc.lpszClassName=L"UpperKeyboardOptions";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    RECT size={0,0,860,488};AdjustWindowRect(&size,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE);
    key_window=CreateWindowW(wc.lpszClassName,L"Teclado - controles e Reset",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,host_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",L"Jogador:",WS_CHILD|WS_VISIBLE,16,16,100,24,key_window,NULL,wc.hInstance,NULL);key_player_selector=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,122,10,300,160,key_window,(HMENU)300,wc.hInstance,NULL);SendMessageW(key_player_selector,CB_ADDSTRING,0,(LPARAM)L"Jogador 1");SendMessageW(key_player_selector,CB_ADDSTRING,0,(LPARAM)L"Jogador 2");SendMessageW(key_player_selector,CB_SETCURSEL,key_edit_player,0);
    for(unsigned i=0;i<KEY_ACTIONS;i++){int col=i/10,row=i%10,x=16+col*430;CreateWindowW(L"STATIC",key_names[i],WS_CHILD|WS_VISIBLE,x,66+row*32,210,24,key_window,NULL,wc.hInstance,NULL);key_buttons[i]=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,x+214,60+row*32,190,28,key_window,(HMENU)(UINT_PTR)(100+i),wc.hInstance,NULL);}
    key_status=CreateWindowW(L"STATIC",L"Clique numa a\u00e7\u00e3o para escolher sua tecla. Ajustes salvos automaticamente.\nF11 / Alt+Enter: tela cheia. F10: menus. Esc: sair. Esses atalhos ficam reservados.\nO jogo pausa enquanto esta janela est\u00e1 aberta. Reset volta ao in\u00edcio e encerra a partida.",WS_CHILD|WS_VISIBLE,16,392,828,60,key_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"BUTTON",L"Restaurar teclado",WS_CHILD|WS_VISIBLE,16,458,400,26,key_window,(HMENU)200,wc.hInstance,NULL);CreateWindowW(L"BUTTON",L"Remover tecla selecionada",WS_CHILD|WS_VISIBLE,446,458,398,26,key_window,(HMENU)201,wc.hInstance,NULL);
    key_refresh();ShowWindow(key_window,SW_SHOW);
}
static unsigned key_game_bits_player(unsigned player,const unsigned char *down){unsigned bits=0;unsigned *map=key_map(player);for(unsigned i=0;i<KEY_ACTIONS;i++)if(map[i]&&down[map[i]])bits|=key_outputs[i];return bits;}
static unsigned key_game_bits(const unsigned char *down){return key_game_bits_player(0,down);}
static void key_poll(void){
    memcpy(key_before,key_down,sizeof(key_down));for(unsigned v=0;v<256;v++)key_down[v]=(GetAsyncKeyState(v)&0x8000)!=0;
    if(key_window&&GetForegroundWindow()==key_window){
        if(key_down[VK_ESCAPE]&&!key_before[VK_ESCAPE]){if(key_capture>=0){key_capture=-1;SetWindowTextW(key_status,L"Altera\u00e7\u00e3o cancelada.");}else DestroyWindow(key_window);return;}
        if(key_capture>=0){
            if(!key_capture_armed){int any=0;for(unsigned v=8;v<255;v++)if(key_down[v])any=1;if(!any)key_capture_armed=1;}
            else for(unsigned v=8;v<255;v++)if(key_down[v]&&!key_before[v]){
                if(!key_allowed(v)){SetWindowTextW(key_status,L"Tecla reservada. Escolha outra tecla; Esc cancela.");break;}
                key_assign(key_capture,v);key_refresh();key_capture=-1;SetWindowTextW(key_status,L"Tecla salva. Escolha outra a\u00e7\u00e3o ou feche para jogar.");break;
            }
        }return;
    }
    if(!key_hotkeys_armed){int held=0;for(unsigned p=0;p<2;p++)for(unsigned i=17;i<19;i++)if(key_map(p)[i]&&key_down[key_map(p)[i]])held=1;if(!held)key_hotkeys_armed=1;return;}
    if(!pad_window&&!key_window&&GetForegroundWindow()==host_window){
        int pause_edge=0,reset_edge=0;for(unsigned p=0;p<2;p++){unsigned pause=key_map(p)[17],reset=key_map(p)[18];if(pause&&key_down[pause]&&!key_before[pause])pause_edge=1;if(reset&&key_down[reset]&&!key_before[reset])reset_edge=1;}
        if(pause_edge&&!online_active())host_paused=!host_paused;
        if(reset_edge&&!online_active()){host_restart=1;DestroyWindow(host_window);}
    }
}
