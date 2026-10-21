#ifdef UPPER_ROLLBACK
typedef struct {uint32_t frame,own,remote;uint64_t hash;int valid;unsigned count;uint32_t events[64][4];} RollbackFrame;
static RollbackFrame rollback_frames[NET_SLOTS];
static int rollback_initialized;static int64_t rollback_confirmed=-1;static uint32_t rollback_audio_next;
static unsigned long long rollback_corrections,rollback_replayed,rollback_predictions;static unsigned rollback_peak;
static unsigned char *rollback_audio_queue;
static int rollback_enqueue(const NetPacket *p){
 EnterCriticalSection(&net_lock);unsigned next=(net_out_write+1)%NET_SLOTS;
 if(next==net_out_read){LeaveCriticalSection(&net_lock);net_fail("Fila de rollback cheia.");return 0;}
 net_outgoing[net_out_write]=*p;net_out_write=next;LeaveCriticalSection(&net_lock);return 1;
}
static int rollback_init(void){
 if(rollback_initialized)return 1;
 if(!snap_init()){net_fail("Nao foi possivel reservar memoria para rollback.");return 0;}
 rollback_audio_queue=calloc(1,0x35000);if(!rollback_audio_queue){net_fail("Sem memoria para audio confirmado.");return 0;}
 rollback_initialized=1;return 1;
}
static unsigned rollback_remote_bits(uint32_t frame){
 unsigned bits=0;EnterCriticalSection(&net_lock);NetSlot *r=net_remote+frame%NET_SLOTS;
 if(r->valid&&r->frame==frame)bits=r->bits;
 else if(frame){RollbackFrame *previous=rollback_frames+(frame-1)%NET_SLOTS;if(previous->valid&&previous->frame==frame-1)bits=previous->remote;}
 LeaveCriticalSection(&net_lock);return bits;
}
static int rollback_store(uint32_t frame){
 RollbackFrame *h=rollback_frames+frame%NET_SLOTS;unsigned char *q=*(void **)(uintptr_t)UINT64_C(0x1422c82c8);
 h->hash=net_hash(training_state,0x1b528);h->count=*(unsigned *)(q+0x34ab4);if(h->count>64){net_fail("Eventos de audio invalidos no rollback.");return 0;}
 memcpy(h->events,q+0x346b4,h->count*16);
 if(!snap_commit()){net_fail("Falha ao salvar o estado do rollback.");return 0;}return 1;
}
static int rollback_replay(uint32_t frame){
 RollbackFrame *h=rollback_frames+frame%NET_SLOTS;if(!h->valid||h->frame!=frame||!snap_begin(frame))return 0;
 h->remote=rollback_remote_bits(frame);unsigned p[2];p[net_config.role]=h->own;p[1-net_config.role]=h->remote;versus_before_tick(&p[0],&p[1]);
 unsigned char *c=training_context,*fr=*(void **)(uintptr_t)UINT64_C(0x141c14f90),*q=*(void **)(uintptr_t)UINT64_C(0x1422c82c8);
 *(unsigned *)(c+0x218)=p[0];*(unsigned *)(c+0x21c)=p[1];*(unsigned *)(c+0x220)=0;
 *(void **)(fr+0xded30)=*(void **)(fr+0xded18);*(unsigned *)(fr+0xded38)=0;*(unsigned *)(fr+0xded3c)=0;*(unsigned *)(q+0x34ab4)=0;
 ((void(*)(void))*(uintptr_t *)(c+0x10))();((void(*)(void))*(uintptr_t *)(c+0x18))();
 rollback_replayed++;return rollback_store(frame);
}
/* Capture a stable confirmation horizon before replay. Newly arrived inputs
   are considered next time; they must never confirm an unchecked prediction. */
static int rollback_sync(void){
 uint32_t tip=net_frame,first=tip;int64_t horizon;
 EnterCriticalSection(&net_lock);horizon=net_remote_last;
 for(int64_t f=rollback_confirmed+1;f<(int64_t)tip;f++){
  RollbackFrame *h=rollback_frames+(uint32_t)f%NET_SLOTS;NetSlot *r=net_remote+(uint32_t)f%NET_SLOTS;
  if(!h->valid||h->frame!=(uint32_t)f){LeaveCriticalSection(&net_lock);net_fail("Historico de rollback indisponivel.");return 0;}
  if(r->valid&&r->frame==(uint32_t)f&&r->bits!=h->remote){first=(uint32_t)f;break;}
 }
 LeaveCriticalSection(&net_lock);
 if(first<tip){
  unsigned depth=tip-first;if(depth>ROLLBACK_MAX||!snap_restore(first,tip)){net_fail("Janela de rollback excedida ou estado indisponivel.");return 0;}
  rollback_corrections++;if(depth>rollback_peak)rollback_peak=depth;
  for(uint32_t f=first;f<tip;f++)if(!rollback_replay(f)){net_fail("Falha ao reexecutar os quadros.");return 0;}
 }
 if(horizon>=(int64_t)tip)horizon=(int64_t)tip-1;
 for(int64_t f=rollback_confirmed+1;f<=horizon;f++){
  RollbackFrame *h=rollback_frames+(uint32_t)f%NET_SLOTS;
  if(!h->valid||h->frame!=(uint32_t)f){net_fail("Estado confirmado indisponivel.");return 0;}
  rollback_confirmed=f;
#ifdef UPPER_TEST_CONFIRMED
  UPPER_TEST_CONFIRMED((uint32_t)f,h->hash);
#endif
  if(((unsigned)f&7)==7||f==0){NetPacket packet;net_packet(&packet,NET_HASH);packet.w[4]=(uint32_t)f;packet.w[5]=(uint32_t)h->hash;packet.w[6]=(uint32_t)(h->hash>>32);if(!rollback_enqueue(&packet))return 0;}
 }
 EnterCriticalSection(&net_lock);int mismatch=0;
 for(int64_t f=rollback_confirmed>64?rollback_confirmed-64:0;f<=rollback_confirmed;f++){
  NetSlot *r=net_remote_hash+(uint32_t)f%NET_SLOTS;RollbackFrame *h=rollback_frames+(uint32_t)f%NET_SLOTS;
  if(r->valid&&r->frame==(uint32_t)f&&h->valid&&h->frame==(uint32_t)f&&r->hash!=h->hash){mismatch=1;break;}
 }
 LeaveCriticalSection(&net_lock);if(mismatch){net_fail("Os estados confirmados divergiram. Sessao interrompida.");return 0;}
 return net_state==NET_READY;
}
static int rollback_step(unsigned *p1,unsigned *p2){
 if(!rollback_init()||!rollback_sync())return -1;
 unsigned local=(net_config.profile?*p2:*p1)&0xfaf9u;uint32_t frame=net_frame;
 if(!net_submitted){
  net_local_hash[frame%NET_SLOTS]=(NetSlot){frame,local,0,1};NetPacket packet;net_packet(&packet,NET_INPUT);packet.w[4]=frame+net_shared_delay;packet.w[5]=local;
  if(!rollback_enqueue(&packet))return -1;net_submitted=1;
 }
 EnterCriticalSection(&net_lock);int64_t last=net_remote_last;NetSlot remote=net_remote[frame%NET_SLOTS];LeaveCriticalSection(&net_lock);
 if((int64_t)frame-last>ROLLBACK_MAX){if(!net_wait_at){net_wait_at=GetTickCount64();net_wait_events++;}return 0;}
 unsigned own=frame>=net_shared_delay?net_local_hash[(frame-net_shared_delay)%NET_SLOTS].bits:0;
 unsigned other=rollback_remote_bits(frame);if(!remote.valid||remote.frame!=frame)rollback_predictions++;
 RollbackFrame *h=rollback_frames+frame%NET_SLOTS;*h=(RollbackFrame){.frame=frame,.own=own,.remote=other,.valid=1};
 if(!snap_begin(frame)){net_fail("Estado de rollback indisponivel.");return -1;}
 if(net_config.role){*p1=other;*p2=own;}else{*p1=own;*p2=other;}
 EnterCriticalSection(&net_lock);net_frame++;net_submitted=0;LeaveCriticalSection(&net_lock);
 if(net_wait_at){net_wait_total+=GetTickCount64()-net_wait_at;net_wait_at=0;}return 1;
}
static int rollback_after_tick(void){
 if(!online_active())return 1;
 if(!rollback_store(net_frame-1)||!rollback_sync())return 0;return 1;
}
static void rollback_audio_frame(void){
#ifdef UPPER_AUDIO
 while((int64_t)rollback_audio_next<=rollback_confirmed){RollbackFrame *h=rollback_frames+rollback_audio_next%NET_SLOTS;
  memcpy(rollback_audio_queue+0x346b4,h->events,h->count*16);*(unsigned *)(rollback_audio_queue+0x34ab4)=h->count;audio_process_events(rollback_audio_queue);rollback_audio_next++;
 }
 audio_output_frame();
#endif
}
static void rollback_shutdown(void){
 printf("ROLLBACK_RESULT frames=%u confirmed=%lld predictions=%llu corrections=%llu replayed=%llu peak=%u max_window=%u peak_snapshot_kib=%llu\n",net_frame,(long long)rollback_confirmed,rollback_predictions,rollback_corrections,rollback_replayed,rollback_peak,ROLLBACK_MAX,snap_peak_pages*4);fflush(stdout);
 snap_close();free(rollback_audio_queue);rollback_audio_queue=NULL;
}
#endif
