/* WebRTC owns ICE/STUN, DTLS peer authentication and reliable ordered SCTP.
   The Cloudflare WebSocket is used only for the offer and answer, then closed. */
#ifdef UPPER_P2P
#define RTC_STATIC
#include <rtc/rtc.h>
static int p2p_pc=-1,p2p_dc=-1;
static volatile LONG p2p_opened,p2p_gathered,p2p_connected;
static HANDLE p2p_rx_event;
static NetPacket p2p_incoming[NET_SLOTS];static unsigned p2p_read,p2p_write;
static char p2p_path[40];static unsigned long long p2p_signal_sent,p2p_signal_received;
static void RTC_API p2p_gather(int pc,rtcGatheringState state,void *ptr){(void)pc;(void)ptr;if(state==RTC_GATHERING_COMPLETE)InterlockedExchange(&p2p_gathered,1);}
static void RTC_API p2p_state(int pc,rtcState state,void *ptr){(void)pc;(void)ptr;if(net_running&&(state==RTC_FAILED||state==RTC_CLOSED)){net_fail("Conexao direta encerrada ou bloqueada pela rede.");if(p2p_rx_event)SetEvent(p2p_rx_event);}}
static void RTC_API p2p_closed(int dc,void *ptr){(void)dc;(void)ptr;if(net_running){net_fail("O outro jogador encerrou a conexao direta.");SetEvent(p2p_rx_event);}}
static void RTC_API p2p_error(int dc,const char *error,void *ptr){(void)dc;(void)error;(void)ptr;if(net_running){net_fail("Falha no canal direto entre os PCs.");SetEvent(p2p_rx_event);}}
static void RTC_API p2p_message(int dc,const char *message,int size,void *ptr){
 (void)dc;(void)ptr;if(!net_running)return;
 if(size!=sizeof(NetPacket)){net_fail("Mensagem P2P invalida.");SetEvent(p2p_rx_event);return;}
 EnterCriticalSection(&net_lock);unsigned next=(p2p_write+1)%NET_SLOTS;
 if(next==p2p_read){LeaveCriticalSection(&net_lock);net_fail("Fila P2P cheia.");SetEvent(p2p_rx_event);return;}
 memcpy(&p2p_incoming[p2p_write],message,sizeof(NetPacket));p2p_write=next;SetEvent(p2p_rx_event);LeaveCriticalSection(&net_lock);
}
static void RTC_API p2p_open(int dc,void *ptr){(void)ptr;if(net_running&&dc==p2p_dc)InterlockedExchange(&p2p_opened,1);}
static int p2p_callbacks(int dc){
 return rtcSetOpenCallback(dc,p2p_open)>=0&&rtcSetClosedCallback(dc,p2p_closed)>=0&&rtcSetErrorCallback(dc,p2p_error)>=0&&rtcSetMessageCallback(dc,p2p_message)>=0;
}
static void RTC_API p2p_channel(int pc,int dc,void *ptr){
 (void)pc;(void)ptr;char label[80];
 if(p2p_dc>=0||rtcGetDataChannelLabel(dc,label,sizeof(label))<0||strcmp(label,"upper3-inputs-v1")){rtcClose(dc);if(net_running)net_fail("Canal P2P inesperado.");return;}
 p2p_dc=dc;if(!p2p_callbacks(dc)&&net_running)net_fail("Nao foi possivel configurar o canal P2P.");
 if(rtcIsOpen(dc))InterlockedExchange(&p2p_opened,1);
}
static int p2p_init(void){p2p_rx_event=CreateEventW(NULL,TRUE,FALSE,NULL);return p2p_rx_event!=NULL;}
static int p2p_wait(volatile LONG *flag,unsigned milliseconds){
 ULONGLONG until=GetTickCount64()+milliseconds;while(net_running&&net_state!=NET_FAILED&&!*flag&&GetTickCount64()<until)Sleep(10);return net_running&&net_state!=NET_FAILED&&*flag;
}
static int p2p_signal_send(char type){
 char description[16384],signal[16384];int n=rtcGetLocalDescription(p2p_pc,description,sizeof(description));
 if(n<=0||strlen(description)>sizeof(signal)-3)return 0;
 signal[0]=type;signal[1]='\n';strcpy(signal+2,description);DWORD length=(DWORD)strlen(signal);
 if(WinHttpWebSocketSend(net_websocket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,signal,length)!=NO_ERROR)return 0;p2p_signal_sent+=length;return 1;
}
static int p2p_signal_receive(char type){
 char signal[16385];unsigned filled=0;
 while(net_running&&filled<16384){DWORD count=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
  if(WinHttpWebSocketReceive(net_websocket,signal+filled,16384-filled,&count,&kind)!=NO_ERROR)return 0;filled+=count;
  if(kind==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE){signal[filled]=0;if(filled<7||signal[0]!=type||signal[1]!='\n'||strncmp(signal+2,"v=0\r\n",5))return 0;p2p_signal_received+=filled;return rtcSetRemoteDescription(p2p_pc,signal+2,type=='O'?"offer":"answer")>=0;}
  if(kind!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE||!count)return 0;
 }return 0;
}
static int p2p_connect(void){
 const char *servers[]={"stun:stun.cloudflare.com:3478"};rtcConfiguration config={0};config.iceServers=servers;config.iceServersCount=1;config.portRangeBegin=48040;config.portRangeEnd=48140;config.maxMessageSize=sizeof(NetPacket);config.mtu=1200;
 rtcInitLogger(RTC_LOG_NONE,NULL);rtcSetThreadPoolSize(2);p2p_pc=rtcCreatePeerConnection(&config);if(p2p_pc<0)return 0;
 if(rtcSetStateChangeCallback(p2p_pc,p2p_state)<0||rtcSetGatheringStateChangeCallback(p2p_pc,p2p_gather)<0||rtcSetDataChannelCallback(p2p_pc,p2p_channel)<0)return 0;
 if(!net_config.role){
  p2p_dc=rtcCreateDataChannel(p2p_pc,"upper3-inputs-v1");if(p2p_dc<0||!p2p_callbacks(p2p_dc))return 0;
  if(!p2p_wait(&p2p_gathered,15000)||!p2p_signal_send('O')||!p2p_signal_receive('A'))return 0;
 }else if(!p2p_signal_receive('O')||!p2p_wait(&p2p_gathered,15000)||!p2p_signal_send('A'))return 0;
 if(!p2p_wait(&p2p_opened,30000)){if(net_running&&net_state!=NET_FAILED)net_fail("A rede bloqueou a conexao direta. Tente IP local ou VPN. Nao foi usado relay.");return 0;}
 char local[1024],remote[1024];if(rtcGetSelectedCandidatePair(p2p_pc,local,sizeof(local),remote,sizeof(remote))<0)return 0;
 if(strstr(local," typ relay")||strstr(remote," typ relay")){net_fail("Caminho por relay recusado nesta versao.");return 0;}
 snprintf(p2p_path,sizeof(p2p_path),"P2P direto");
 /* The selected path is logged without private addresses, SDP or invite. */
 printf("P2P_CONNECTED path=%s signal_sent_bytes=%llu signal_received_bytes=%llu\n",p2p_path,p2p_signal_sent,p2p_signal_received);fflush(stdout);InterlockedExchange(&p2p_connected,1);
 HINTERNET socket=net_websocket;net_websocket=NULL;WinHttpCloseHandle(socket);return 1;
}
static int p2p_send(const NetPacket *wire){return p2p_dc>=0&&p2p_opened&&rtcGetBufferedAmount(p2p_dc)<65536&&rtcSendMessage(p2p_dc,(const char *)wire,sizeof(*wire))>=0;}
static int p2p_receive(NetPacket *wire){
 while(net_running&&net_state!=NET_FAILED){EnterCriticalSection(&net_lock);
  if(p2p_read!=p2p_write){*wire=p2p_incoming[p2p_read];p2p_read=(p2p_read+1)%NET_SLOTS;if(p2p_read==p2p_write)ResetEvent(p2p_rx_event);LeaveCriticalSection(&net_lock);return 1;}
  LeaveCriticalSection(&net_lock);if(GetTickCount64()-net_received_at>15000)return 0;WaitForSingleObject(p2p_rx_event,100);
 }return 0;
}
static void p2p_close(void){
 if(p2p_dc>=0){rtcDeleteDataChannel(p2p_dc);p2p_dc=-1;}
 if(p2p_pc>=0){rtcDeletePeerConnection(p2p_pc);p2p_pc=-1;}
 if(p2p_rx_event){CloseHandle(p2p_rx_event);p2p_rx_event=NULL;}
}
#else
static char p2p_path[1];
static int p2p_init(void){return 0;}
static int p2p_connect(void){return 0;}
static int p2p_send(const NetPacket *packet){(void)packet;return 0;}
static int p2p_receive(NetPacket *packet){(void)packet;return 0;}
static void p2p_close(void){}
#endif
