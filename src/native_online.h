#ifdef UPPER_ONLINE
#include "native_net_transport.h"
#include "native_rollback.h"
static int online_requested;
static HWND online_window,online_address,online_port,online_delay,online_profile,online_code,online_service,online_invite;
static wchar_t online_launch_arguments[1536];
static int online_active(void){return online_requested;}
static HWND lobby_window;
static int online_config_open(void){return online_window!=NULL||lobby_window!=NULL;}
static void online_close_window(void){if(online_window)DestroyWindow(online_window);if(lobby_window)DestroyWindow(lobby_window);}
static int online_parse(unsigned argc,char **argv){
    if(argc<2||strncmp(argv[1],"--net-",6))return 1;
    online_requested=1;memset(&net_config,0,sizeof(net_config));net_config.delay=2;net_config.port=48030;
    if(!strcmp(argv[1],"--net-host")&&argc==6){net_config.role=0;net_config.port=strtoul(argv[2],NULL,10);net_config.delay=strtoul(argv[3],NULL,10);net_config.profile=strtoul(argv[4],NULL,10);snprintf(net_config.code,sizeof(net_config.code),"%s",argv[5]);}
    else if(!strcmp(argv[1],"--net-join")&&argc==6){net_config.role=1;snprintf(net_config.address,sizeof(net_config.address),"%s",argv[2]);net_config.port=strtoul(argv[3],NULL,10);net_config.profile=strtoul(argv[4],NULL,10);snprintf(net_config.code,sizeof(net_config.code),"%s",argv[5]);struct in_addr address;if(InetPtonA(AF_INET,net_config.address,&address)!=1)return 0;}
    else if(!strcmp(argv[1],"--net-p2p-ready")&&argc==9){
        net_config.relay=2;snprintf(net_config.address,sizeof(net_config.address),"%s",argv[2]);
        if(!net_hex_valid(argv[3],32)||!net_hex_valid(argv[4],64)||strlen(argv[5])!=1||(*argv[5]!='0'&&*argv[5]!='1'))return 0;
        strcpy(net_config.room,argv[3]);strcpy(net_config.token,argv[4]);net_config.role=*argv[5]-'0';net_config.delay=strtoul(argv[6],NULL,10);net_config.profile=strtoul(argv[7],NULL,10);
        if(strcmp(argv[8],"lobby1"))return 0;
    }
    else if((!strcmp(argv[1],"--net-relay-host")||!strcmp(argv[1],"--net-p2p-host"))&&argc==5){net_config.relay=!strcmp(argv[1],"--net-p2p-host")?2:1;snprintf(net_config.address,sizeof(net_config.address),"%s",argv[2]);net_config.delay=strtoul(argv[3],NULL,10);net_config.profile=strtoul(argv[4],NULL,10);}
    else if((!strcmp(argv[1],"--net-relay-join")||!strcmp(argv[1],"--net-p2p-join"))&&argc==4){
        net_config.role=1;net_config.relay=!strcmp(argv[1],"--net-p2p-join")?2:1;net_config.profile=strtoul(argv[3],NULL,10);char invite[768];if(strlen(argv[2])>=sizeof(invite))return 0;strcpy(invite,argv[2]);char *parts[4],*p=invite;
        for(unsigned i=0;i<4;i++){parts[i]=p;char *end=strchr(p,'|');if(i<3){if(!end)return 0;*end=0;p=end+1;}else if(end)return 0;}
        if(strcmp(parts[0],net_config.relay==2?"U3P1":"U3N1")||strlen(parts[1])>=sizeof(net_config.address)||!net_hex_valid(parts[2],32)||!net_hex_valid(parts[3],64))return 0;
        strcpy(net_config.address,parts[1]);strcpy(net_config.room,parts[2]);strcpy(net_config.token,parts[3]);
    }else return 0;
    if(net_config.port<1024||net_config.port>65535||net_config.delay>8||net_config.profile>1)return 0;
    for(const char *p=net_config.code;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'))return 0;
    if(!net_config.relay&&!net_config.code[0])return 0;
    if(net_config.relay){
        if(strncmp(net_config.address,"https://",8)&&strncmp(net_config.address,"http://127.0.0.1:",17))return 0;
        for(const char *p=net_config.address;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-.:/",*p)))return 0;
    }return 1;
}
static void online_launch(HWND w,const wchar_t *arguments){wcsncpy(online_launch_arguments,arguments,1535);host_restart=1;DestroyWindow(w);}
#include "native_lobby.h"
static void online_copy_invite(void){
    char invite[768];EnterCriticalSection(&net_lock);snprintf(invite,sizeof(invite),"%s",net_config.invite);LeaveCriticalSection(&net_lock);
    if(!invite[0]){MessageBoxW(host_window,L"Aguarde a sala ser criada antes de copiar o convite.",L"Online",MB_OK);return;}
    if(OpenClipboard(host_window)){HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,strlen(invite)+1);if(memory){void *p=GlobalLock(memory);memcpy(p,invite,strlen(invite)+1);GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_TEXT,memory))GlobalFree(memory);}CloseClipboard();}
}
static LRESULT CALLBACK online_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_COMMAND){unsigned id=LOWORD(wp);
        if(id>=601&&id<=604){
            wchar_t address[128],code[65],port[16],service[256],invite[768],args[1536];
            GetWindowTextW(online_address,address,128);GetWindowTextW(online_code,code,65);GetWindowTextW(online_port,port,16);GetWindowTextW(online_service,service,256);GetWindowTextW(online_invite,invite,768);
            unsigned number=wcstoul(port,NULL,10),delay=(unsigned)SendMessageW(online_delay,CB_GETCURSEL,0,0),profile=(unsigned)SendMessageW(online_profile,CB_GETCURSEL,0,0);
            if(id<603&&(number<1024||number>65535||!code[0])){MessageBoxW(w,L"Informe uma porta entre 1024 e 65535 e um c\u00f3digo de acesso.",L"Online",MB_OK);return 0;}
            if(id==601)swprintf(args,1536,L"--net-host %u %u %u %ls",number,delay,profile,code);
            else if(id==602)swprintf(args,1536,L"--net-join %ls %u %u %ls",address,number,profile,code);
            else if(id==603)swprintf(args,1536,L"--net-p2p-host %ls %u %u",service,delay,profile);
            else swprintf(args,1536,L"--net-p2p-join %ls %u",invite,profile);
            /* Validate before closing the running local match. All allowed
               values are ASCII and contain no quotes, spaces or shell syntax. */
            char narrow[1536];if(!WideCharToMultiByte(CP_UTF8,0,args,-1,narrow,sizeof(narrow),NULL,NULL))return 0;char *values[10]={"game"};unsigned count=1;char *token=strtok(narrow," ");while(token&&count<10){values[count++]=token;token=strtok(NULL," ");}
            NetConfig saved=net_config;int was_requested=online_requested;int valid=online_parse(count,values);net_config=saved;online_requested=was_requested;
            if(!valid){MessageBoxW(w,L"Confira o IPv4, o c\u00f3digo (letras/n\u00fameros/h\u00edfen), o servidor ou o convite completo.",L"Online",MB_OK);return 0;}
            online_launch(host_window,args);return 0;
        }
    }
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}if(msg==WM_DESTROY){online_window=NULL;return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
static HWND online_edit(const wchar_t *label,int y,int width,const wchar_t *value){
    CreateWindowW(L"STATIC",label,WS_CHILD|WS_VISIBLE,18,y+3,180,24,online_window,NULL,GetModuleHandle(NULL),NULL);
    HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",value,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,206,y,width,26,online_window,NULL,GetModuleHandle(NULL),NULL);return edit;
}
static void online_open(void){
    if(online_window){SetForegroundWindow(online_window);return;}
    WNDCLASSW wc={0};wc.lpfnWndProc=online_proc;wc.hInstance=GetModuleHandle(NULL);wc.lpszClassName=L"UpperOnlineOptions";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    online_window=CreateWindowW(wc.lpszClassName,L"Online experimental - dois jogadores",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,720,575,host_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",L"Conex\u00e3o direta: um jogador cria, o outro informa IPv4, porta e c\u00f3digo.\nPela internet pode ser necess\u00e1rio abrir a porta TCP no roteador ou usar uma VPN.",WS_CHILD|WS_VISIBLE,18,14,670,42,online_window,NULL,wc.hInstance,NULL);
    online_address=online_edit(L"IPv4 de quem cria",64,460,L"127.0.0.1");online_port=online_edit(L"Porta TCP",98,130,L"48030");
    wchar_t code[16];unsigned random=0;BCryptGenRandom(NULL,(PUCHAR)&random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG);swprintf(code,16,L"%08x",random);online_code=online_edit(L"C\u00f3digo de acesso",132,230,code);
    CreateWindowW(L"STATIC",L"Fila de comandos",WS_CHILD|WS_VISIBLE,18,170,180,25,online_window,NULL,wc.hInstance,NULL);online_delay=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,206,166,460,240,online_window,NULL,wc.hInstance,NULL);
    for(unsigned i=0;i<=8;i++){wchar_t label[80];swprintf(label,80,L"%u quadros (%.0f ms adicionados aos comandos)",i,i*1000.0/60);SendMessageW(online_delay,CB_ADDSTRING,0,(LPARAM)label);}SendMessageW(online_delay,CB_SETCURSEL,
#ifdef UPPER_ROLLBACK
    1,
#else
    2,
#endif
    0);
    CreateWindowW(L"STATIC",L"Seu perfil de controle",WS_CHILD|WS_VISIBLE,18,205,180,25,online_window,NULL,wc.hInstance,NULL);online_profile=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,206,200,460,120,online_window,NULL,wc.hInstance,NULL);SendMessageW(online_profile,CB_ADDSTRING,0,(LPARAM)L"Perfil local do jogador 1");SendMessageW(online_profile,CB_ADDSTRING,0,(LPARAM)L"Perfil local do jogador 2");SendMessageW(online_profile,CB_SETCURSEL,0,0);
    CreateWindowW(L"BUTTON",L"Criar partida direta (P1)",WS_CHILD|WS_VISIBLE,18,240,320,30,online_window,(HMENU)601,wc.hInstance,NULL);CreateWindowW(L"BUTTON",L"Entrar por IPv4 (P2)",WS_CHILD|WS_VISIBLE,350,240,320,30,online_window,(HMENU)602,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",L"Convite P2P: Cloudflare conecta; comandos seguem direto entre os PCs.",WS_CHILD|WS_VISIBLE,18,294,680,24,online_window,NULL,wc.hInstance,NULL);
    online_service=online_edit(L"Servidor de salas",324,460,L"https://alpha3-upper-netplay.andre-oka.workers.dev");online_invite=online_edit(L"Convite recebido",362,460,L"");
    CreateWindowW(L"BUTTON",L"Criar convite P2P (P1)",WS_CHILD|WS_VISIBLE,18,402,320,30,online_window,(HMENU)603,wc.hInstance,NULL);CreateWindowW(L"BUTTON",L"Entrar com convite P2P (P2)",WS_CHILD|WS_VISIBLE,350,402,320,30,online_window,(HMENU)604,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",L"Permita UDP no firewall para o convite P2P. Algumas redes exigem VPN.\nCriar usa os rounds de quem hospeda. Pausa / Reset ficam desativados.\n"
#ifdef UPPER_ROLLBACK
    L"Rollback at\u00e9 8 quadros; som confirmado. Use a mesma vers\u00e3o nos dois PCs.",
#else
    L"Sem rollback. Ajuste controles antes de conectar. Use a mesma vers\u00e3o nos PCs.",
#endif
    WS_CHILD|WS_VISIBLE,18,452,670,64,online_window,NULL,wc.hInstance,NULL);
    ShowWindow(online_window,SW_SHOW);SetForegroundWindow(online_window);
}
static void online_add_menu(HMENU bar){
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(online_active()?MF_GRAYED:0),5100,L"Criar / entrar em partida...");
    AppendMenuW(menu,MF_STRING|(online_active()?MF_GRAYED:0),5103,L"Lobby e chat...");
    if(online_active()){AppendMenuW(menu,MF_STRING|(net_config.relay&&!net_config.role?0:MF_GRAYED),5101,L"Copiar convite da sala");AppendMenuW(menu,MF_STRING,5102,L"Desconectar e voltar ao local");AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"Pausa / Reset locais desativados na sess\u00e3o");}
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)menu,L"Online");
}
static int online_step(unsigned *p1,unsigned *p2,unsigned char *context,unsigned char *state){
    if(!online_active())return 1;
    if(net_state!=NET_READY)return net_state==NET_FAILED?-1:0;
    if(net_state==NET_READY&&!net_settings_applied){option_training=0;option_versus=1;option_rounds=net_rounds;option_difficulty=net_difficulty;options_apply_game(context);*(unsigned short *)(state+0x70)=(unsigned short)net_seed;net_settings_applied=1;}
#ifdef UPPER_ROLLBACK
    return rollback_step(p1,p2);
#else
    unsigned players[2];unsigned local=net_config.profile?*p2:*p1;int result=net_poll(local,net_hash(state,0x1b528),players);
    if(result==1){*p1=players[0];*p2=players[1];}return result;
#endif
}
static void online_paint(HDC dc,RECT view){
    if(!online_active())return;wchar_t text[320];
    if(net_state==NET_READY)swprintf(text,320,
#ifdef UPPER_ROLLBACK
    L"Online P%u | RTT %ld ms | fila %u q\n%hs | %ls\nEsperas %llu (%llu ms)\nRollback %llu | reexec. %llu | pico %u/8",net_config.role+1,net_rtt,net_shared_delay,net_config.relay==2?p2p_path:"TCP direto",net_wait_at?L"Aguardando comandos":L"Conectado",net_wait_events,net_wait_total+(net_wait_at?GetTickCount64()-net_wait_at:0),rollback_corrections,rollback_replayed,rollback_peak);
#else
    L"Online P%u | RTT %ld ms | fila %u q\n%hs | %ls\nEsperas %llu (%llu ms)",net_config.role+1,net_rtt,net_shared_delay,net_config.relay==2?p2p_path:net_config.relay?"Relay antigo":"TCP direto",net_wait_at?L"Aguardando comandos":L"Conectado",net_wait_events,net_wait_total+(net_wait_at?GetTickCount64()-net_wait_at:0));
#endif
    else if(net_state==NET_FAILED)swprintf(text,320,L"Online: %hs\nOnline > Desconectar para voltar ao modo local",net_error);
    else swprintf(text,320,net_config.relay?L"Online: criando/conectando sala...\nQuem cria: Online > Copiar convite da sala":L"Online: aguardando conex\u00e3o TCP na porta %u...\nQuem entra precisa do IPv4 e do c\u00f3digo combinado.",net_config.port);
    int height=view.bottom-view.top,size=height/44;if(size<11)size=11;if(size>19)size=19;HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");HGDIOBJ old=SelectObject(dc,font);
    RECT r={view.left+6,view.top+6,view.left+(net_state==NET_FAILED?view.right-view.left-12:size*30),view.top+size*6+12};FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));InflateRect(&r,-6,-3);SetBkMode(dc,TRANSPARENT);training_text(dc,text,r,DT_LEFT|DT_WORDBREAK,net_state==NET_FAILED?RGB(255,130,100):RGB(110,240,160));SelectObject(dc,old);DeleteObject(font);
}
#else
static wchar_t online_launch_arguments[2];
static int online_active(void){return 0;}
static int online_config_open(void){return 0;}
static void online_close_window(void){}
static int online_parse(unsigned argc,char **argv){(void)argc;(void)argv;return 1;}
static void online_add_menu(HMENU bar){(void)bar;}
static void online_open(void){}
static void online_paint(HDC dc,RECT view){(void)dc;(void)view;}
static int online_step(unsigned *p1,unsigned *p2,unsigned char *c,unsigned char *s){(void)p1;(void)p2;(void)c;(void)s;return 1;}
static void net_close(void){}
#endif
