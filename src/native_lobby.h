/* Lobby IO is separate from the game transport. Only the UI thread touches
   windows; bounded queues bridge it to WinHTTP. Match traffic stays P2P. */
static HWND lobby_name,lobby_log,lobby_list,lobby_message,lobby_status,lobby_profile;
static CRITICAL_SECTION lobby_lock;
static int lobby_initialized;
static volatile LONG lobby_running;
static HANDLE lobby_thread;
static HINTERNET lobby_socket;
static char lobby_nick[128],lobby_me[17],lobby_ids[64][17];
static char lobby_rx[64][8192],lobby_tx[32][1024];
static unsigned lobby_rr,lobby_rw,lobby_tr,lobby_tw;
static unsigned lobby_generation;
static const wchar_t *lobby_url=L"https://alpha3-upper-netplay.andre-oka.workers.dev";
static int lobby_push(const char *text,int incoming){
    int ok=0;EnterCriticalSection(&lobby_lock);
    if(incoming){unsigned next=(lobby_rw+1)%64;if(next!=lobby_rr&&strlen(text)<8192){strcpy(lobby_rx[lobby_rw],text);lobby_rw=next;ok=1;}}
    else{unsigned next=(lobby_tw+1)%32;if(next!=lobby_tr&&strlen(text)<1024){strcpy(lobby_tx[lobby_tw],text);lobby_tw=next;ok=1;}}
    LeaveCriticalSection(&lobby_lock);return ok;
}
static void lobby_note(const wchar_t *text){
    if(GetWindowTextLengthW(lobby_log)>24000)SetWindowTextW(lobby_log,L"");
    SendMessageW(lobby_log,EM_SETSEL,-1,-1);SendMessageW(lobby_log,EM_REPLACESEL,FALSE,(LPARAM)text);SendMessageW(lobby_log,EM_REPLACESEL,FALSE,(LPARAM)L"\r\n");SendMessageW(lobby_log,EM_SCROLLCARET,0,0);
}
static DWORD WINAPI lobby_sender(void *unused){
    (void)unused;ULONGLONG ping=GetTickCount64();
    while(lobby_running){
        char message[1024]={0};EnterCriticalSection(&lobby_lock);
        if(lobby_tr!=lobby_tw){strcpy(message,lobby_tx[lobby_tr]);lobby_tr=(lobby_tr+1)%32;}
        else if(GetTickCount64()-ping>=15000){strcpy(message,"PING");ping=GetTickCount64();}
        if(message[0]&&lobby_socket&&WinHttpWebSocketSend(lobby_socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,message,(DWORD)strlen(message))!=NO_ERROR)InterlockedExchange(&lobby_running,0);
        LeaveCriticalSection(&lobby_lock);Sleep(30);
    }return 0;
}
static DWORD WINAPI lobby_worker(void *unused){
    (void)unused;HINTERNET session=NULL,connection=NULL,request=NULL,ws=NULL;HANDLE sender=NULL;int published=0;
    wchar_t host[256],path[256];URL_COMPONENTS u={0};u.dwStructSize=sizeof(u);u.lpszHostName=host;u.dwHostNameLength=255;
    if(!WinHttpCrackUrl(lobby_url,0,0,&u))goto done;host[u.dwHostNameLength]=0;
    if(u.nScheme!=INTERNET_SCHEME_HTTPS&&wcscmp(host,L"127.0.0.1"))goto done;
    session=WinHttpOpen(L"Alpha3UpperLobby/0.15",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)goto done;
    WinHttpSetTimeouts(session,3000,3000,3000,3000);connection=WinHttpConnect(session,host,u.nPort,0);if(!connection)goto done;
    wcscpy(path,L"/lobby/public-v1");request=WinHttpOpenRequest(connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,u.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);if(!request)goto done;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0)||!WinHttpSendRequest(request,L"X-Upper3-Client: 1\r\n",(DWORD)-1,NULL,0,0,0)||!WinHttpReceiveResponse(request,NULL))goto done;
    DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&size,NULL)||status!=101)goto done;
    ws=WinHttpWebSocketCompleteUpgrade(request,0);if(!ws)goto done;
    DWORD timeout=30000;WinHttpSetOption(ws,WINHTTP_OPTION_RECEIVE_TIMEOUT,&timeout,sizeof(timeout));
    EnterCriticalSection(&lobby_lock);if(lobby_running){lobby_socket=ws;published=1;}LeaveCriticalSection(&lobby_lock);if(!lobby_running)goto done;
    char name[180];snprintf(name,sizeof(name),"NAME\t0.16.0\t%s",lobby_nick);if(!lobby_push(name,0))goto done;
    sender=CreateThread(NULL,0,lobby_sender,NULL,0,NULL);if(!sender)goto done;
    while(lobby_running){
        char text[8192];DWORD used=0;int complete=0;
        while(lobby_running&&used<sizeof(text)-1){DWORD n=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            if(WinHttpWebSocketReceive(ws,text+used,sizeof(text)-1-used,&n,&kind)!=NO_ERROR)goto done;
            used+=n;if(kind==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE){complete=1;break;}
            if(kind!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE||!n)goto done;
        }
        if(!complete)goto done;text[used]=0;if(strlen(text)!=used||!lobby_push(text,1))goto done;
    }
done:
    InterlockedExchange(&lobby_running,0);
    if(sender){WaitForSingleObject(sender,INFINITE);CloseHandle(sender);}
    EnterCriticalSection(&lobby_lock);if(lobby_socket==ws)lobby_socket=NULL;else if(published)ws=NULL;LeaveCriticalSection(&lobby_lock);
    if(ws)WinHttpCloseHandle(ws);if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);
    lobby_push("OFF\tDesconectado. Clique em Entrar para reconectar.",1);return 0;
}
static void lobby_stop(void){
    if(!lobby_initialized)return;InterlockedExchange(&lobby_running,0);
    EnterCriticalSection(&lobby_lock);HINTERNET socket=lobby_socket;lobby_socket=NULL;if(socket)WinHttpCloseHandle(socket);LeaveCriticalSection(&lobby_lock);
    if(lobby_thread){WaitForSingleObject(lobby_thread,INFINITE);CloseHandle(lobby_thread);lobby_thread=NULL;}
}
static unsigned lobby_split(char *s,char **p,unsigned max){unsigned n=0;while(n<max){p[n++]=s;char *e=strchr(s,'\t');if(!e)break;*e=0;s=e+1;}return n;}
static void lobby_drain(void){
    for(unsigned count=0;count<64&&lobby_window;count++){
        char text[8192],*p[200];int have=0;EnterCriticalSection(&lobby_lock);
        if(lobby_rr!=lobby_rw){strcpy(text,lobby_rx[lobby_rr]);lobby_rr=(lobby_rr+1)%64;have=1;}LeaveCriticalSection(&lobby_lock);if(!have)break;
        unsigned n=lobby_split(text,p,200);wchar_t line[1024];
        if(!strcmp(p[0],"YOU")&&n==2&&net_hex_valid(p[1],16)){strcpy(lobby_me,p[1]);SetWindowTextW(lobby_status,L"Conectado ao lobby");EnableWindow(lobby_name,FALSE);}
        else if(!strcmp(p[0],"LIST")&&(n-1)%3==0){
            SendMessageW(lobby_list,LB_RESETCONTENT,0,0);unsigned j=0;
            for(unsigned i=1;i+2<n&&j<64;i+=3)if(net_hex_valid(p[i],16)&&strcmp(p[i],lobby_me)){
                wchar_t name[128],status[64];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p[i+1],-1,name,128)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p[i+2],-1,status,64))continue;swprintf(line,1024,L"%ls (%ls)",name,status);strcpy(lobby_ids[j++],p[i]);SendMessageW(lobby_list,LB_ADDSTRING,0,(LPARAM)line);
            }
        }
        else if(!strcmp(p[0],"CHAT")&&n==3){wchar_t name[128],body[512];if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p[1],-1,name,128)&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p[2],-1,body,512)){swprintf(line,1024,L"%ls: %ls",name,body);lobby_note(line);}}
        else if((!strcmp(p[0],"NOTICE")||!strcmp(p[0],"ERROR")||!strcmp(p[0],"OFF"))&&n==2){if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p[1],-1,line,1024))lobby_note(line);
            if(!strcmp(p[0],"OFF")){SetWindowTextW(lobby_status,L"Desconectado");EnableWindow(lobby_name,TRUE);SendMessageW(lobby_list,LB_RESETCONTENT,0,0);}}
        else if(!strcmp(p[0],"INVITE")&&n==3&&net_hex_valid(p[1],16)){
            char id[17];strcpy(id,p[1]);wchar_t name[128];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p[2],-1,name,128))continue;
            swprintf(line,1024,L"%ls convidou voc\u00ea para jogar versus. Aceitar?\n\nA partida atual ser\u00e1 encerrada. O convite expira em 30 segundos.",name);
            unsigned generation=lobby_generation;HWND window=lobby_window;int yes=MessageBoxW(window,line,L"Convite para partida",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;
            if(lobby_window!=window||generation!=lobby_generation)return;char command[64];snprintf(command,sizeof(command),"%s\t%s",yes?"ACCEPT":"DECLINE",id);lobby_push(command,0);
        }
        else if(!strcmp(p[0],"START")&&n==4&&strlen(p[1])==1&&(*p[1]=='0'||*p[1]=='1')&&net_hex_valid(p[2],32)&&net_hex_valid(p[3],64)){
            wchar_t args[1536];unsigned profile=(unsigned)SendMessageW(lobby_profile,CB_GETCURSEL,0,0);if(profile>1)profile=0;
            swprintf(args,1536,L"--net-p2p-ready %ls %hs %hs %hs 1 %u lobby1",lobby_url,p[2],p[3],p[1],profile);
            lobby_stop();online_launch(host_window,args);return;
        }
    }
}
static WNDPROC lobby_edit_proc;
static LRESULT CALLBACK lobby_message_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_KEYDOWN&&wp==VK_RETURN){SendMessageW(lobby_window,WM_COMMAND,702,0);return 0;}if(msg==WM_CHAR&&wp=='\r')return 0;return CallWindowProcW(lobby_edit_proc,w,msg,wp,lp);}
static LRESULT CALLBACK lobby_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_TIMER){lobby_drain();return 0;}
    if(msg==WM_COMMAND){unsigned id=LOWORD(wp);
        if(id==701){
            if(lobby_running)return 0;lobby_stop();wchar_t nick[64];GetWindowTextW(lobby_name,nick,64);
            if(!nick[0]||wcslen(nick)>20||wcspbrk(nick,L"\t\r\n")||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,nick,-1,lobby_nick,sizeof(lobby_nick),NULL,NULL)){MessageBoxW(w,L"Informe um apelido de 1 a 20 caracteres.",L"Lobby",MB_OK);return 0;}
            EnterCriticalSection(&lobby_lock);lobby_rr=lobby_rw=lobby_tr=lobby_tw=0;LeaveCriticalSection(&lobby_lock);lobby_me[0]=0;
            InterlockedExchange(&lobby_running,1);lobby_thread=CreateThread(NULL,0,lobby_worker,NULL,0,NULL);if(!lobby_thread)InterlockedExchange(&lobby_running,0);SetWindowTextW(lobby_status,L"Conectando...");return 0;
        }
        if(id==702&&lobby_running&&lobby_me[0]){wchar_t message[256];char command[1024];GetWindowTextW(lobby_message,message,256);
            if(message[0]&&!wcspbrk(message,L"\t\r\n")){strcpy(command,"CHAT\t");if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,message,-1,command+5,sizeof(command)-5,NULL,NULL)&&lobby_push(command,0))SetWindowTextW(lobby_message,L"");}return 0;}
        if(id==703&&lobby_running){LRESULT i=SendMessageW(lobby_list,LB_GETCURSEL,0,0);if(i>=0&&i<64){char target[17];strcpy(target,lobby_ids[i]);
            if(MessageBoxW(w,L"Convidar este jogador? Ao aceitar, a partida local ser\u00e1 encerrada e o versus online come\u00e7ar\u00e1.",L"Convidar",MB_YESNO|MB_DEFBUTTON2)==IDYES){char command[64];snprintf(command,sizeof(command),"INVITE\t%s",target);lobby_push(command,0);}}return 0;}
        if(id==704){lobby_stop();return 0;}
    }
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}
    if(msg==WM_DESTROY){KillTimer(w,1);lobby_stop();lobby_window=NULL;lobby_generation++;return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
static HWND lobby_control(const wchar_t *type,const wchar_t *text,DWORD style,int x,int y,int width,int height,unsigned id){HWND h=CreateWindowExW(!wcscmp(type,L"EDIT")||!wcscmp(type,L"LISTBOX")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,lobby_window,(HMENU)(UINT_PTR)id,GetModuleHandle(NULL),NULL);SendMessageW(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return h;}
static void lobby_open(void){
    if(lobby_window){ShowWindow(lobby_window,SW_SHOW);SetForegroundWindow(lobby_window);return;}
    if(!lobby_initialized){InitializeCriticalSection(&lobby_lock);lobby_initialized=1;}
    WNDCLASSW wc={0};wc.lpfnWndProc=lobby_proc;wc.hInstance=GetModuleHandle(NULL);wc.lpszClassName=L"UpperLobby";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    lobby_window=CreateWindowW(wc.lpszClassName,L"Alpha 3 Upper - Lobby e chat",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,850,610,host_window,NULL,wc.hInstance,NULL);
    lobby_control(L"STATIC",L"Apelido",0,16,18,60,24,0);lobby_name=lobby_control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,80,14,200,26,0);SendMessageW(lobby_name,EM_SETLIMITTEXT,20,0);
    lobby_control(L"BUTTON",L"Entrar",WS_TABSTOP,290,14,85,28,701);lobby_control(L"BUTTON",L"Sair",WS_TABSTOP,385,14,85,28,704);lobby_status=lobby_control(L"STATIC",L"Desconectado",0,490,18,325,24,0);
    lobby_control(L"STATIC",L"Chat geral",0,16,57,500,22,0);lobby_control(L"STATIC",L"Jogadores no lobby",0,560,57,260,22,0);
    lobby_log=lobby_control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,16,82,526,350,0);
    lobby_list=lobby_control(L"LISTBOX",L"",WS_TABSTOP|LBS_NOTIFY|WS_VSCROLL,560,82,255,270,0);
    lobby_control(L"BUTTON",L"Convidar para versus",WS_TABSTOP,560,362,255,30,703);
    lobby_control(L"STATIC",L"Seu perfil de controle",0,560,408,255,20,0);lobby_profile=lobby_control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,560,432,255,100,0);SendMessageW(lobby_profile,CB_ADDSTRING,0,(LPARAM)L"Perfil local do jogador 1");SendMessageW(lobby_profile,CB_ADDSTRING,0,(LPARAM)L"Perfil local do jogador 2");SendMessageW(lobby_profile,CB_SETCURSEL,0,0);
    lobby_message=lobby_control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,16,444,424,28,0);SendMessageW(lobby_message,EM_SETLIMITTEXT,240,0);lobby_edit_proc=(WNDPROC)SetWindowLongPtrW(lobby_message,GWLP_WNDPROC,(LONG_PTR)lobby_message_proc);lobby_control(L"BUTTON",L"Enviar",WS_TABSTOP,450,444,92,28,702);
    lobby_control(L"STATIC",L"Apelidos n\u00e3o verificados. Chat p\u00fablico, sem hist\u00f3rico salvo.\nAo aceitar: conex\u00e3o P2P com rollback e fila de 1 quadro.\nO convite individual continua em Online > Criar / entrar em partida.",0,16,492,800,65,0);
    SetTimer(lobby_window,1,200,NULL);ShowWindow(lobby_window,SW_SHOW);SetForegroundWindow(lobby_window);
}
