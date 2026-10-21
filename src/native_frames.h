/* Observes every original simulation step, including extra Turbo steps. */
static FILE *frame_trace;
static unsigned frame_observed;
static int frame_attack_box(unsigned char *a){
    uintptr_t ptr=*(uintptr_t *)(a+0x1a0);
    if(!((ptr>=0x08231000&&ptr+2<=0x18231000)||(ptr>=UINT64_C(0x140000000)&&ptr+2<=UINT64_C(0x144000000))))return 0;
    return a[0]&&a[0xb3]==0&&a[0x14a]==0&&((unsigned char *)ptr)[1]!=0;
}

#define FRAME_CELLS 96
/* One cell is an original simulation update. Hitstop is shown separately and
   excluded from move startup/active/recovery counts. */
enum {FRAME_FREE,FRAME_START,FRAME_ACTIVE,FRAME_RECOVERY,FRAME_HIT,FRAME_GUARD,FRAME_AIR,FRAME_STOP,FRAME_LINK};
typedef struct {
    unsigned doing,previous_stop,in_move,had_active,total,first_active,active,last_active;
    unsigned shown_start,shown_active,shown_recovery,shown_at,free_at,contact,guarded;
} FrameTrack;
static FrameTrack frame_tracks[2];
static unsigned char frame_cells[2][FRAME_CELLS];
static unsigned frame_length,frame_idle=30,frame_clock,frame_suspend;
static int frame_advantage,frame_advantage_player=-1,frame_advantage_guarded;
static unsigned frame_advantage_at,frame_pending,frame_pending_victim;
static unsigned frame_totals[2][9],frame_completed;
static void frame_reset(void){
    memset(frame_tracks,0,sizeof(frame_tracks));memset(frame_cells,0,sizeof(frame_cells));memset(frame_totals,0,sizeof(frame_totals));
    frame_length=frame_clock=frame_pending=frame_completed=0;frame_idle=30;frame_advantage_player=-1;
}
static unsigned frame_classify(unsigned p,unsigned char *a){
    FrameTrack *t=&frame_tracks[p];unsigned stop=t->previous_stop;t->previous_stop=a[0x9f]!=0;
    unsigned before=t->doing;
    if(a[4]!=1){t->in_move=0;return FRAME_AIR;}
    if(a[5]!=0){
        t->in_move=0;t->contact=1;t->guarded=a[0x94]==1;
        return stop?FRAME_STOP:t->guarded?FRAME_GUARD:FRAME_HIT;
    }
    if(a[6]<=2&&a[0x59]==0){
        if(t->in_move&&t->had_active){
            t->shown_start=t->first_active;t->shown_active=t->active;t->shown_recovery=t->total-t->last_active;t->shown_at=frame_clock;frame_completed++;
        }
        t->in_move=0;
        if(before!=FRAME_FREE){t->free_at=frame_clock;if(t->contact){frame_pending=1;frame_pending_victim=p;t->contact=0;}}
        return FRAME_FREE;
    }
    if(a[0x59]&&a[6]<5)return FRAME_AIR;
    if(a[6]<5||(a[6]==6&&a[0x94]==1))return FRAME_AIR;
    if(stop)return FRAME_STOP;
    if(!t->in_move){t->in_move=1;t->had_active=0;t->shown_at=0;frame_advantage_player=-1;t->total=t->first_active=t->active=t->last_active=0;}
    t->total++;
    if(frame_attack_box(a)){
        if(!t->had_active){t->had_active=1;t->first_active=t->total;}
        t->active++;t->last_active=t->total;return FRAME_ACTIVE;
    }
    return t->had_active?FRAME_RECOVERY:FRAME_START;
}
static void frame_observe(void){
    if(frame_suspend||!training_in_battle()||*(unsigned short *)(training_state+0x2c)!=3||*(unsigned short *)(training_state+0x2e)!=3)return;
    frame_observed++;frame_clock++;
    unsigned doing[2];
    for(unsigned p=0;p<2;p++){unsigned char *a=training_state+0x1f8+p*0x580;doing[p]=frame_classify(p,a);frame_tracks[p].doing=doing[p];frame_totals[p][doing[p]]++;}
    if(frame_pending){
        unsigned victim=frame_pending_victim,attacker=1-victim;
        if(doing[victim]!=FRAME_FREE)frame_pending=0;
        else if(doing[attacker]==FRAME_FREE){frame_pending=0;frame_advantage=(int)frame_tracks[victim].free_at-(int)frame_tracks[attacker].free_at;frame_advantage_player=(int)attacker;frame_advantage_guarded=frame_tracks[victim].guarded;frame_advantage_at=frame_clock;}
    }
    if(doing[0]==FRAME_FREE&&doing[1]==FRAME_FREE){if(frame_idle<30)frame_idle++;}
    else{
        if(frame_idle>=30)frame_length=0;frame_idle=0;
        if(frame_length==FRAME_CELLS){for(unsigned p=0;p<2;p++)memmove(frame_cells[p],frame_cells[p]+1,FRAME_CELLS-1);frame_length--;}
        for(unsigned p=0;p<2;p++){
            unsigned cell=doing[p];if(cell==FRAME_FREE&&(doing[1-p]==FRAME_HIT||doing[1-p]==FRAME_GUARD))cell=FRAME_LINK;
            frame_cells[p][frame_length]=(unsigned char)cell;
        }frame_length++;
    }
    if(frame_trace){
        fprintf(frame_trace,"%u,%u,%u",frame_observed,active_step,training_state[0x89]);
        for(unsigned p=0;p<2;p++){
            unsigned char *a=training_state+0x1f8+p*0x580;
            fprintf(frame_trace,",%u,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",a[4],a[5],a[6],a[7],a[0x59],*(short *)(a+0x62),*(short *)(a+0x90),a[0x94],a[0x9e],a[0x9f],a[0xb4],a[0x1fb],a[0x1fd],frame_attack_box(a),a[0x14c],a[0x2fa],a[0x2fb],*(unsigned short *)(a+0x1f2));
        }fputc('\n',frame_trace);
    }
}
static void frame_native_step(void){
    ((void (*)(void))(uintptr_t)UINT64_C(0x140e10b60))();frame_observe();
}
static void frame_install(void){
    if(!option_training)return;
    /* Both original update paths end their actor/collision update at this call.
       Observe once per simulation step, never once per window refresh. */
    unsigned char *relay=VirtualAlloc((void *)(uintptr_t)UINT64_C(0x144100000),4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!relay)ExitProcess(73);host_patch((uintptr_t)relay,frame_native_step);
    const uintptr_t calls[]={UINT64_C(0x140df9e92),UINT64_C(0x140eb5da3)};
    for(unsigned i=0;i<2;i++){
        unsigned char *at=(unsigned char *)calls[i];int32_t old_rel;memcpy(&old_rel,at+1,4);
        if(at[0]!=0xe8||(uintptr_t)(at+5)+old_rel!=UINT64_C(0x140e10b60))ExitProcess(71);
        int64_t distance=relay-(at+5);if(distance<INT32_MIN||distance>INT32_MAX)ExitProcess(72);
        int32_t relative=(int32_t)distance;DWORD old;VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old);memcpy(at+1,&relative,4);VirtualProtect(at,5,old,&old);FlushInstructionCache(GetCurrentProcess(),at,5);
    }
    if(GetEnvironmentVariableA("UPPER_FRAME_TRACE",NULL,0)){
        frame_trace=fopen("data/frame-trace.csv","w");
        if(frame_trace){fprintf(frame_trace,"step,host,freeze");for(unsigned p=0;p<2;p++)fprintf(frame_trace,",p%u_s4,p%u_s5,p%u_s6,p%u_s7,p%u_air,p%u_t62,p%u_hp,p%u_s94,p%u_s9e,p%u_s9f,p%u_b4,p%u_1fb,p%u_cpu,p%u_box,p%u_14c,p%u_2fa,p%u_2fb,p%u_input",p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p);fputc('\n',frame_trace);}
    }
}

static void frame_dump(void){
    if(!option_training)return;
    FILE *f=fopen("data/frame-meter.json","w");if(!f)return;
    fprintf(f,"{\"observed\":%u,\"steps\":%u,\"length\":%u,\"advantage\":%d,\"advantage_player\":%d,\"guarded\":%d,\"players\":[",frame_observed,frame_clock,frame_length,frame_advantage,frame_advantage_player,frame_advantage_guarded);
    for(unsigned p=0;p<2;p++){FrameTrack *t=&frame_tracks[p];fprintf(f,"%s{\"startup\":%u,\"active\":%u,\"recovery\":%u,\"counts\":[",p?",":"",t->shown_start,t->shown_active,t->shown_recovery);for(unsigned i=0;i<9;i++)fprintf(f,"%s%u",i?",":"",frame_totals[p][i]);fprintf(f,"],\"cells\":[");for(unsigned i=0;i<frame_length;i++)fprintf(f,"%s%u",i?",":"",frame_cells[p][i]);fprintf(f,"]}");}fprintf(f,"]}\n");fclose(f);
}
