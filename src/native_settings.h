/* User preferences belong to the standalone host, beside its resource files. */
static volatile LONG option_effects=50,option_music=100;
static unsigned option_difficulty=3,option_rounds=3;
static unsigned option_versus=0;
static unsigned option_training=0,option_dummy=0,option_guard=0,option_regen=1,option_meter=1,option_train_stats=1,option_train_inputs=1;
static unsigned option_hitboxes=0,option_frame_meter=1;
static unsigned option_fullscreen=0,option_aspect=0,option_crt=0;
static unsigned option_performance=0;
static char option_file[MAX_PATH];
static void options_load(void){
    GetFullPathNameA("data/settings.ini",MAX_PATH,option_file,NULL);
    unsigned fx=GetPrivateProfileIntA("Audio","EfeitosVozes",50,option_file);
    unsigned bgm=GetPrivateProfileIntA("Audio","Musica",100,option_file);
    option_effects=fx<=100?fx:50;option_music=bgm<=100?bgm:100;
    unsigned d=GetPrivateProfileIntA("Partida","Dificuldade",3,option_file);
    unsigned r=GetPrivateProfileIntA("Partida","RoundsParaVencer",3,option_file);
    option_training=GetPrivateProfileIntA("Treino","Ativo",0,option_file)==1;
    option_versus=!option_training&&GetPrivateProfileIntA("Partida","Versus",0,option_file)==1;
    option_dummy=GetPrivateProfileIntA("Treino","Adversario",0,option_file);if(option_dummy>4)option_dummy=0;
    option_guard=GetPrivateProfileIntA("Treino","Defesa",0,option_file);if(option_guard>2)option_guard=0;
    option_regen=GetPrivateProfileIntA("Treino","RecuperarVida",1,option_file)==1;
    option_meter=GetPrivateProfileIntA("Treino","BarraEspecial",1,option_file);if(option_meter>2)option_meter=1;
    option_train_stats=GetPrivateProfileIntA("Treino","Dados",1,option_file)==1;
    option_train_inputs=GetPrivateProfileIntA("Treino","Comandos",1,option_file)==1;
    option_hitboxes=GetPrivateProfileIntA("Treino","Hitboxes",0,option_file)==1;
    option_frame_meter=GetPrivateProfileIntA("Treino","MedidorQuadros",1,option_file)==1;
    option_difficulty=d<=8?d:8;option_rounds=r>=1&&r<=3?r:3;
    option_fullscreen=GetPrivateProfileIntA("Tela","Fullscreen",0,option_file)==1;
    option_aspect=GetPrivateProfileIntA("Tela","Wide",0,option_file)==1;
    option_crt=GetPrivateProfileIntA("Tela","FiltroCRT",0,option_file);if(option_crt>4)option_crt=0;
    option_performance=GetPrivateProfileIntA("Diagnostico","Mostrar",0,option_file)==1;
}
static void options_save_audio(int music,unsigned percent){
    if(percent>100)return;
    char value[8];snprintf(value,sizeof(value),"%u",percent);
    InterlockedExchange(music?&option_music:&option_effects,(LONG)percent);
    WritePrivateProfileStringA("Audio",music?"Musica":"EfeitosVozes",value,option_file);
}

static void options_save_game(int rounds,unsigned value){
    if((rounds&&value<1)||value>(rounds?3u:8u))return;
    if(rounds)option_rounds=value;else option_difficulty=value;
    char text[12];snprintf(text,sizeof(text),"%u",value);
    WritePrivateProfileStringA("Partida",rounds?"RoundsParaVencer":"Dificuldade",text,option_file);
}
/* Collection training configuration consumed by the original game callbacks.
   0x878/87c: native life regeneration; 880/884: native special-meter mode. */
static void options_apply_training(unsigned char *context){
    static const unsigned native_dummy[]={0,1,2,6,5};
    *(unsigned *)(context+0x850)=native_dummy[option_dummy];
    *(unsigned *)(context+0x858)=option_guard;
    *(unsigned *)(context+0x8c8)=!option_hitboxes; /* Original collision-box visibility flag. */
    *(unsigned *)(context+0x878)=option_regen;*(unsigned *)(context+0x87c)=option_regen;
    *(unsigned *)(context+0x880)=option_meter;*(unsigned *)(context+0x884)=option_meter;
}
static void options_apply_mode(unsigned char *context){
    *(unsigned *)(context+0x828)=option_training;
    /* Original collection versus entry: both players, no arcade credit gate. */
    *(unsigned *)(context+0x7c0)=option_versus&&!option_training;
    if(option_training){
        *(unsigned *)(context+0x82c)=0;*(unsigned *)(context+0x838)=0;
        options_apply_training(context);
    }
}
static void options_save_mode(unsigned versus,unsigned training){
    option_versus=versus!=0;option_training=training!=0;
    if(option_training)option_versus=0;
    WritePrivateProfileStringA("Partida","Versus",option_versus?"1":"0",option_file);
    WritePrivateProfileStringA("Treino","Ativo",option_training?"1":"0",option_file);
}
static void options_save_training(void){
    const char *names[]={"Ativo","Adversario","Defesa","RecuperarVida","BarraEspecial","Dados","Comandos","Hitboxes","MedidorQuadros"};
    unsigned values[]={option_training,option_dummy,option_guard,option_regen,option_meter,option_train_stats,option_train_inputs,option_hitboxes,option_frame_meter};
    for(unsigned i=0;i<9;i++){char v[12];snprintf(v,sizeof(v),"%u",values[i]);WritePrivateProfileStringA("Treino",names[i],v,option_file);}
}
static void options_apply_game(unsigned char *context){
    options_apply_mode(context);
    /* Original boot derives the difficulty rank and adaptive-difficulty flag from 0x28c.
       dfb540 independently maps the speed selector 0x298 to state+0x60 / state+0xb6.
       Value 2 enables the original in-game Turbo level selection. */
    *(unsigned *)(context+0x28c)=option_difficulty;
    *(unsigned *)(context+0x4dc)=option_difficulty?option_difficulty-1:0;
    *(unsigned *)(context+0x4e4)=option_difficulty==0;
    *(unsigned *)(context+0x298)=2;
    *(unsigned *)(context+0x29c)=option_rounds-1;
    printf("GAME_OPTIONS difficulty=%u rounds_to_win=%u\n",option_difficulty,option_rounds);
}

static void options_save_display(void){
    char value[8];
    snprintf(value,sizeof(value),"%u",option_fullscreen);WritePrivateProfileStringA("Tela","Fullscreen",value,option_file);
    snprintf(value,sizeof(value),"%u",option_aspect);WritePrivateProfileStringA("Tela","Wide",value,option_file);
    snprintf(value,sizeof(value),"%u",option_crt);WritePrivateProfileStringA("Tela","FiltroCRT",value,option_file);
}
