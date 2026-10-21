/* Experimental transport. Fixed-size, bounded packets; never accepts executable
   code, file names or memory addresses from a peer. Main thread never waits on IO. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <winhttp.h>
#define NET_WORDS 32
#define NET_SLOTS 512
#define NET_MAGIC 0x55334e50u
#ifdef UPPER_ROLLBACK
#define NET_PROTOCOL 2u
#else
#define NET_PROTOCOL 1u
#endif
enum {NET_HELLO=1,NET_INPUT,NET_PING,NET_PONG,NET_QUIT,NET_HASH};
enum {NET_OFF,NET_CONNECTING,NET_READY,NET_FAILED};
typedef struct {uint32_t w[NET_WORDS];} NetPacket;
typedef struct {uint32_t frame,bits;uint64_t hash;int valid;} NetSlot;
typedef struct {int role,relay;unsigned port,delay,profile;char address[256],code[65],room[33],token[65],invite[768];} NetConfig;
static NetConfig net_config;
static volatile LONG net_state,net_running,net_rtt;
static HANDLE net_thread,net_receiver;
static SOCKET net_socket=INVALID_SOCKET,net_listener=INVALID_SOCKET;
static HINTERNET net_http_session,net_http_connection,net_websocket;
static CRITICAL_SECTION net_lock,net_send_lock;
static int net_initialized,net_settings_applied,net_submitted;
static uint32_t net_frame,net_seed,net_rounds,net_difficulty,net_shared_delay;
static NetSlot net_remote[NET_SLOTS],net_local_hash[NET_SLOTS],net_remote_hash[NET_SLOTS];
#ifdef UPPER_ROLLBACK
static int64_t net_remote_last=-1,net_remote_hash_last=-1;static uint32_t net_remote_next;
#endif
static NetPacket net_outgoing[NET_SLOTS];static unsigned net_out_read,net_out_write;
static char net_error[240];static unsigned char net_identity[32],net_code_identity[32];
static ULONGLONG net_received_at,net_wait_at,net_wait_total;static unsigned long long net_wait_events;
#ifdef UPPER_ROLLBACK
static void rollback_shutdown(void);
#endif
static uint64_t net_hash(const unsigned char *p,size_t n){uint64_t h=UINT64_C(14695981039346656037);while(n--){h^=*p++;h*=UINT64_C(1099511628211);}return h;}
static void net_fail(const char *message){
    EnterCriticalSection(&net_lock);if(net_state!=NET_FAILED){snprintf(net_error,sizeof(net_error),"%s",message);InterlockedExchange(&net_state,NET_FAILED);}LeaveCriticalSection(&net_lock);
}
static void net_packet(NetPacket *p,unsigned type){memset(p,0,sizeof(*p));p->w[0]=NET_MAGIC;p->w[1]=NET_PROTOCOL;p->w[2]=type;p->w[3]=sizeof(*p);}
static int net_valid_packet(const NetPacket *p){return p->w[0]==NET_MAGIC&&p->w[1]==NET_PROTOCOL&&p->w[3]==sizeof(*p)&&p->w[2]>=NET_HELLO&&p->w[2]<=
#ifdef UPPER_ROLLBACK
NET_HASH
#else
NET_QUIT
#endif
;}
static int net_sha(const wchar_t **files,unsigned count,const void *text,size_t length,unsigned char digest[32]){
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;int ok=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)return 0;
    if(BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0)goto done;
    if(text&&BCryptHashData(hash,(PUCHAR)text,(ULONG)length,0)<0)goto done;
    for(unsigned i=0;i<count;i++){
        FILE *f=_wfopen(files[i],L"rb");if(!f)goto done;unsigned char buffer[65536];size_t n;
        size_t remaining=SIZE_MAX;if(wcsstr(files[i],L"main.bin")){if(fseek(f,0x1000,SEEK_SET)){fclose(f);goto done;}remaining=0xfdb000;}
        while(remaining&&(n=fread(buffer,1,remaining<sizeof(buffer)?remaining:sizeof(buffer),f))!=0){if(BCryptHashData(hash,buffer,(ULONG)n,0)<0){fclose(f);goto done;}if(remaining!=SIZE_MAX)remaining-=n;}
        if(remaining!=SIZE_MAX&&remaining){fclose(f);goto done;}
        int error=ferror(f);fclose(f);if(error)goto done;
    }
    ok=BCryptFinishHash(hash,digest,32,0)>=0;
done:if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
static int net_hex_valid(const char *s,unsigned n){if(strlen(s)!=n)return 0;for(unsigned i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
#include "native_p2p.h"
static int net_send(const NetPacket *packet){
    NetPacket wire=*packet;for(unsigned i=0;i<NET_WORDS;i++)wire.w[i]=htonl(wire.w[i]);int ok=1;
    EnterCriticalSection(&net_send_lock);
    if(net_config.relay==2){ok=p2p_send(&wire);}
    else if(net_config.relay){ok=WinHttpWebSocketSend(net_websocket,WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,&wire,sizeof(wire))==NO_ERROR;}
    else{unsigned sent=0;while(sent<sizeof(wire)&&net_running){int n=send(net_socket,(const char *)&wire+sent,sizeof(wire)-sent,0);if(n<=0){ok=0;break;}sent+=(unsigned)n;}if(sent!=sizeof(wire))ok=0;}
    LeaveCriticalSection(&net_send_lock);return ok;
}
static int net_receive(NetPacket *p){
    unsigned filled=0;if(net_config.relay==2){if(!p2p_receive(p))return 0;filled=sizeof(*p);}
    while(filled<sizeof(*p)&&net_running){
        if(net_config.relay){
            DWORD n=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            DWORD result=WinHttpWebSocketReceive(net_websocket,(char *)p+filled,sizeof(*p)-filled,&n,&type);
            if(result!=NO_ERROR||type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)return 0;
            if(type!=WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE&&type!=WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)return 0;
            filled+=n;if(type==WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE&&filled!=sizeof(*p))return 0;
            if(filled==sizeof(*p)&&type!=WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)return 0;
        }else{
            int n=recv(net_socket,(char *)p+filled,sizeof(*p)-filled,0);
            if(n==0)return 0;
            if(n<0){if(WSAGetLastError()==WSAETIMEDOUT&&GetTickCount64()-net_received_at<15000)continue;return 0;}
            filled+=(unsigned)n;
        }
    }
    if(filled!=sizeof(*p))return 0;
    for(unsigned i=0;i<NET_WORDS;i++)p->w[i]=ntohl(p->w[i]);
    if(!net_valid_packet(p))return 0;net_received_at=GetTickCount64();return 1;
}
static int net_connect_direct(void){
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return 0;
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((u_short)net_config.port);
    if(net_config.role==0){
        address.sin_addr.s_addr=htonl(INADDR_ANY);net_listener=s;
        if(bind(s,(struct sockaddr *)&address,sizeof(address))||listen(s,1))return 0;
        ULONGLONG started=GetTickCount64();
        while(net_running&&GetTickCount64()-started<300000){fd_set readable;FD_ZERO(&readable);FD_SET(s,&readable);struct timeval t={0,100000};int r=select(0,&readable,NULL,NULL,&t);if(r<0)return 0;if(r>0){net_socket=accept(s,NULL,NULL);break;}}
        closesocket(s);net_listener=INVALID_SOCKET;if(net_socket==INVALID_SOCKET)return 0;
    }else{
        if(InetPtonA(AF_INET,net_config.address,&address.sin_addr)!=1){closesocket(s);return 0;}
        net_socket=s;u_long nonblock=1;ioctlsocket(s,FIONBIO,&nonblock);
        int r=connect(s,(struct sockaddr *)&address,sizeof(address));
        if(r!=0&&WSAGetLastError()!=WSAEWOULDBLOCK)return 0;
        if(r!=0){
            ULONGLONG started=GetTickCount64();int connected=0;
            while(net_running&&GetTickCount64()-started<15000){fd_set writable,errors;FD_ZERO(&writable);FD_ZERO(&errors);FD_SET(s,&writable);FD_SET(s,&errors);struct timeval t={0,100000};r=select(0,NULL,&writable,&errors,&t);if(r<0||FD_ISSET(s,&errors))return 0;if(r>0){int error=0,len=sizeof(error);getsockopt(s,SOL_SOCKET,SO_ERROR,(char *)&error,&len);if(error)return 0;connected=1;break;}}
            if(!connected)return 0;
        }
        nonblock=0;ioctlsocket(s,FIONBIO,&nonblock);
    }
    DWORD receive_ms=1000,send_ms=3000;int yes=1;
    setsockopt(net_socket,IPPROTO_TCP,TCP_NODELAY,(const char *)&yes,sizeof(yes));
    setsockopt(net_socket,SOL_SOCKET,SO_RCVTIMEO,(const char *)&receive_ms,sizeof(receive_ms));setsockopt(net_socket,SOL_SOCKET,SO_SNDTIMEO,(const char *)&send_ms,sizeof(send_ms));return 1;
}
static int net_json_string(const char *json,const char *key,char *out,size_t capacity){
    char pattern[64];snprintf(pattern,sizeof(pattern),"\"%s\":\"",key);const char *p=strstr(json,pattern);if(!p)return 0;p+=strlen(pattern);const char *end=strchr(p,'"');if(!end||(size_t)(end-p)>=capacity)return 0;
    memcpy(out,p,end-p);out[end-p]=0;return 1;
}
static int net_connect_relay(void){
    wchar_t url[512],host[256],path[768];MultiByteToWideChar(CP_UTF8,0,net_config.address,-1,url,512);
    URL_COMPONENTS components={0};components.dwStructSize=sizeof(components);components.lpszHostName=host;components.dwHostNameLength=255;components.dwUrlPathLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url,0,0,&components))return 0;
    host[components.dwHostNameLength]=0;
    if(components.nScheme!=INTERNET_SCHEME_HTTPS&&wcscmp(host,L"127.0.0.1")&&wcscmp(host,L"localhost"))return 0;
    if(components.dwUrlPathLength>1)return 0;
    net_http_session=WinHttpOpen(L"Alpha3UpperOnline/0.13",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!net_http_session)return 0;
    WinHttpSetTimeouts(net_http_session,3000,3000,3000,3000);net_http_connection=WinHttpConnect(net_http_session,host,components.nPort,0);if(!net_http_connection)return 0;
    DWORD flags=components.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0;
    if(net_config.role==0 && !net_config.room[0]){
        HINTERNET request=WinHttpOpenRequest(net_http_connection,L"POST",net_config.relay==2?L"/p2p/rooms":L"/rooms",NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);if(!request)return 0;
        int ok=WinHttpSendRequest(request,L"X-Upper3-Client: 1\r\n",(DWORD)-1,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,NULL);DWORD status=0,size=sizeof(status);
        if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)&&status==201;
        char body[2048]={0};DWORD read=0,total=0;
        while(ok&&total<sizeof(body)-1){if(!WinHttpReadData(request,body+total,sizeof(body)-1-total,&read)){ok=0;break;}if(!read)break;total+=read;}
        WinHttpCloseHandle(request);char guest[65];
        if(!ok||!net_json_string(body,"room",net_config.room,sizeof(net_config.room))||!net_json_string(body,"hostToken",net_config.token,sizeof(net_config.token))||!net_json_string(body,"guestToken",guest,sizeof(guest)))return 0;
        if(!net_hex_valid(net_config.room,32)||!net_hex_valid(net_config.token,64)||!net_hex_valid(guest,64))return 0;
        EnterCriticalSection(&net_lock);snprintf(net_config.invite,sizeof(net_config.invite),"%s|%s|%s|%s",net_config.relay==2?"U3P1":"U3N1",net_config.address,net_config.room,guest);LeaveCriticalSection(&net_lock);
    }
    swprintf(path,768,L"%ls/rooms/%hs/%ls",net_config.relay==2?L"/p2p":L"",net_config.room,net_config.role?L"guest":L"host");
    HINTERNET request=WinHttpOpenRequest(net_http_connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);if(!request)return 0;
    wchar_t auth[128];swprintf(auth,128,L"Authorization: Bearer %hs\r\n",net_config.token);
    int ok=WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0)&&WinHttpSendRequest(request,auth,(DWORD)-1,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,NULL);
    if(ok)net_websocket=WinHttpWebSocketCompleteUpgrade(request,0);WinHttpCloseHandle(request);if(!net_websocket)return 0;
    DWORD timeout=15000;WinHttpSetOption(net_websocket,WINHTTP_OPTION_RECEIVE_TIMEOUT,&timeout,sizeof(timeout));
    /* Relay emits only WAIT then READY text before forwarding binary packets. */
    ULONGLONG started=GetTickCount64();while(net_running&&GetTickCount64()-started<300000){char text[128]={0};DWORD n=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;DWORD result=WinHttpWebSocketReceive(net_websocket,text,sizeof(text)-1,&n,&type);
        if(result!=NO_ERROR)return 0;if(type!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)return 0;text[n]=0;
        if(!strcmp(text,"READY"))return net_config.relay==2?p2p_connect():1;if(strcmp(text,"WAIT"))return 0;
    }return 0;
}
static DWORD WINAPI net_receive_thread(void *unused){
    (void)unused;while(net_running&&net_state==NET_READY){NetPacket p;if(!net_receive(&p)){if(net_running)net_fail("Conexao encerrada ou mensagem invalida.");break;}
        if(p.w[2]==NET_PING){p.w[2]=NET_PONG;if(!net_send(&p))net_fail("Falha ao responder a conexao.");continue;}
        if(p.w[2]==NET_PONG){unsigned rtt=GetTickCount()-p.w[4];if(rtt<30000)InterlockedExchange(&net_rtt,(LONG)rtt);continue;}
#ifdef UPPER_ROLLBACK
        if(p.w[2]==NET_HASH){
            EnterCriticalSection(&net_lock);uint32_t f=p.w[4];int64_t delta=(int64_t)f-net_frame;
            if((int64_t)f<=net_remote_hash_last||delta>NET_SLOTS/2||delta<-(NET_SLOTS/2)){LeaveCriticalSection(&net_lock);net_fail("Hash confirmado fora da sequencia.");break;}
            net_remote_hash_last=f;net_remote_hash[f%NET_SLOTS]=(NetSlot){f,0,(uint64_t)p.w[5]|((uint64_t)p.w[6]<<32),1};LeaveCriticalSection(&net_lock);continue;
        }
        if(p.w[2]!=NET_INPUT){net_fail("Sessao encerrada pelo outro jogador.");break;}
        EnterCriticalSection(&net_lock);uint32_t f=p.w[4];int64_t delta=(int64_t)f-net_frame;
        if((p.w[5]&~0xfaf9u)||f!=net_remote_next||delta>NET_SLOTS/2||delta<-(NET_SLOTS/2)){LeaveCriticalSection(&net_lock);net_fail("Sequencia rollback invalida.");break;}
        net_remote[f%NET_SLOTS]=(NetSlot){f,p.w[5],0,1};net_remote_last=f;net_remote_next++;LeaveCriticalSection(&net_lock);continue;
#else
        if(p.w[2]!=NET_INPUT){net_fail("Sessao encerrada pelo outro jogador.");break;}
        EnterCriticalSection(&net_lock);uint32_t frame=p.w[4],hash_frame=p.w[6];
        if((p.w[5]&~0xfaf9u)||frame<net_frame||frame-net_frame>=NET_SLOTS-16||hash_frame>frame||frame-hash_frame!=net_shared_delay){LeaveCriticalSection(&net_lock);net_fail("Sequencia de comandos invalida.");break;}
        NetSlot *input=&net_remote[frame%NET_SLOTS];if(input->valid&&input->frame==frame){LeaveCriticalSection(&net_lock);net_fail("Comando duplicado.");break;}
        *input=(NetSlot){frame,p.w[5],0,1};uint64_t hash=(uint64_t)p.w[7]|((uint64_t)p.w[8]<<32);
        NetSlot *remote=&net_remote_hash[hash_frame%NET_SLOTS],*local=&net_local_hash[hash_frame%NET_SLOTS];*remote=(NetSlot){hash_frame,0,hash,1};
        int mismatch=local->valid&&local->frame==hash_frame&&local->hash!=hash;LeaveCriticalSection(&net_lock);
        if(mismatch){net_fail("Os jogos perderam a sincronizacao. Sessao interrompida.");break;}
#endif
    }return 0;
}
static DWORD WINAPI net_connect_thread(void *unused){
    (void)unused;wchar_t executable[MAX_PATH];GetModuleFileNameW(NULL,executable,MAX_PATH);const wchar_t *files[]={executable,L"data/main.bin",L"data/arena.bin"};
    if(!net_sha(files,3,NULL,0,net_identity)||!net_sha(NULL,0,net_config.code,strlen(net_config.code),net_code_identity)){net_fail("Nao foi possivel verificar os arquivos do jogo.");return 0;}
    if(!(net_config.relay?net_connect_relay():net_connect_direct())){if(net_running)net_fail("Nao foi possivel conectar. Confira o endereco, porta ou convite; em P2P permita UDP no firewall.");return 0;}
    NetPacket hello,peer;net_packet(&hello,NET_HELLO);hello.w[4]=net_config.role;hello.w[5]=net_config.delay;hello.w[6]=option_rounds;hello.w[7]=option_difficulty;
    unsigned short seed=0;BCryptGenRandom(NULL,(PUCHAR)&seed,sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG);hello.w[8]=seed?seed:0x5371;
    memcpy(hello.w+10,net_identity,32);memcpy(hello.w+18,net_code_identity,32);net_received_at=GetTickCount64();
    if(!net_send(&hello)||!net_receive(&peer)||peer.w[2]!=NET_HELLO){net_fail("A outra instancia nao respondeu ao protocolo do jogo.");return 0;}
    if(peer.w[4]==hello.w[4]||peer.w[4]>1||memcmp(peer.w+10,hello.w+10,32)||memcmp(peer.w+18,hello.w+18,32)||peer.w[5]>8||peer.w[6]<1||peer.w[6]>3||peer.w[7]>8||peer.w[8]>65535){net_fail("Versao, arquivos ou codigo de acesso diferentes entre os jogadores.");return 0;}
    NetPacket *host=net_config.role?&peer:&hello;net_shared_delay=host->w[5];net_rounds=host->w[6];net_difficulty=host->w[7];net_seed=host->w[8];
    for(unsigned i=0;i<net_shared_delay;i++)net_remote[i]=(NetSlot){i,0,0,1};
#ifdef UPPER_ROLLBACK
    net_remote_last=(int64_t)net_shared_delay-1;net_remote_next=net_shared_delay;
#endif
    InterlockedExchange(&net_state,NET_READY);net_receiver=CreateThread(NULL,0,net_receive_thread,NULL,0,NULL);if(!net_receiver){net_fail("Falha ao iniciar a recepcao de comandos.");return 0;}
    ULONGLONG ping_at=0;while(net_running&&net_state==NET_READY){
        NetPacket packet;int have=0;EnterCriticalSection(&net_lock);if(net_out_read!=net_out_write){packet=net_outgoing[net_out_read];net_out_read=(net_out_read+1)%NET_SLOTS;have=1;}LeaveCriticalSection(&net_lock);
        if(have){if(!net_send(&packet)){net_fail("Falha ao enviar comandos.");break;}}
        else Sleep(1);
        ULONGLONG now=GetTickCount64();if(now-ping_at>=1000){net_packet(&packet,NET_PING);packet.w[4]=GetTickCount();if(!net_send(&packet)){net_fail("Falha na conexao.");break;}ping_at=now;}
        if(now-net_received_at>15000){net_fail("A outra instancia deixou de responder.");break;}
    }return 0;
}
static int net_open(void){
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return 0;
    InitializeCriticalSection(&net_lock);InitializeCriticalSection(&net_send_lock);net_initialized=1;
    if(net_config.relay==2&&!p2p_init())return 0;
    InterlockedExchange(&net_running,1);InterlockedExchange(&net_state,NET_CONNECTING);net_thread=CreateThread(NULL,0,net_connect_thread,NULL,0,NULL);return net_thread!=NULL;
}
/* Hashes travel asynchronously; requiring a same-frame hash before advancing
   would add a round-trip barrier and defeat the input-delay pipeline. */
static int net_poll(uint32_t local,uint64_t hash,uint32_t players[2]){
    if(net_state==NET_FAILED)return -1;if(net_state!=NET_READY)return 0;
    EnterCriticalSection(&net_lock);
    if(!net_submitted){
        NetSlot *remote=&net_remote_hash[net_frame%NET_SLOTS];if(remote->valid&&remote->frame==net_frame&&remote->hash!=hash){LeaveCriticalSection(&net_lock);net_fail("Os jogos perderam a sincronizacao. Sessao interrompida.");return -1;}
        net_local_hash[net_frame%NET_SLOTS]=(NetSlot){net_frame,local,hash,1};
        unsigned next=(net_out_write+1)%NET_SLOTS;if(next==net_out_read){LeaveCriticalSection(&net_lock);net_fail("Fila de rede cheia.");return -1;}
        NetPacket *p=&net_outgoing[net_out_write];net_packet(p,NET_INPUT);p->w[4]=net_frame+net_shared_delay;p->w[5]=local&0xfaf9u;p->w[6]=net_frame;p->w[7]=(uint32_t)hash;p->w[8]=(uint32_t)(hash>>32);net_out_write=next;
        net_submitted=1;
    }
    NetSlot *remote=&net_remote[net_frame%NET_SLOTS];if(!remote->valid||remote->frame!=net_frame){LeaveCriticalSection(&net_lock);if(!net_wait_at){net_wait_at=GetTickCount64();net_wait_events++;}return 0;}
    uint32_t own=0;if(net_frame>=net_shared_delay){NetSlot *old=&net_local_hash[(net_frame-net_shared_delay)%NET_SLOTS];own=old->bits;}
    players[net_config.role]=own;players[1-net_config.role]=remote->bits;remote->valid=0;
    net_frame++;net_submitted=0;LeaveCriticalSection(&net_lock);
    if(net_wait_at){net_wait_total+=GetTickCount64()-net_wait_at;net_wait_at=0;}return 1;
}
static void net_close(void){
    if(!net_initialized)return;InterlockedExchange(&net_running,0);
    if(net_listener!=INVALID_SOCKET){closesocket(net_listener);net_listener=INVALID_SOCKET;}
    if(net_socket!=INVALID_SOCKET)shutdown(net_socket,SD_BOTH);
    if(net_websocket){WinHttpCloseHandle(net_websocket);net_websocket=NULL;}
    if(net_thread)WaitForSingleObject(net_thread,10000);if(net_receiver)WaitForSingleObject(net_receiver,4000);
    if(net_socket!=INVALID_SOCKET)closesocket(net_socket);
    if(net_http_connection)WinHttpCloseHandle(net_http_connection);if(net_http_session)WinHttpCloseHandle(net_http_session);
    if(net_thread)CloseHandle(net_thread);if(net_receiver)CloseHandle(net_receiver);
#ifdef UPPER_ROLLBACK
    rollback_shutdown();
#endif
    p2p_close();
    printf("NET_RESULT frames=%u role=%d delay=%u rtt_ms=%ld wait_events=%llu wait_ms=%llu state=%ld\n",net_frame,net_config.role,net_shared_delay,net_rtt,net_wait_events,net_wait_total,net_state);fflush(stdout);
    WSACleanup();
}
