/* The game owns training rules, recovery, CPU and combat statistics.
   This window configures its existing collection interface. */
static HWND training_window,training_dummy,training_guard,training_regen,training_meter,training_stats,training_inputs,training_hitboxes,training_meter_frames;
static unsigned char *training_context,*training_state;
static int training_reset_requested,training_select_armed,training_record_mode;
static void training_record_action(unsigned action);
typedef struct {unsigned bits,frames;} TrainingInput;
static TrainingInput training_history[2][7];
static int training_in_battle(void){return option_training&&training_state&&*(unsigned short *)(training_state+0x28)==1&&*(unsigned short *)(training_state+0x2a)==2;}
#include "native_frames.h"
static void training_read_options(void){
    option_dummy=(unsigned)SendMessageW(training_dummy,CB_GETCURSEL,0,0);
    option_guard=(unsigned)SendMessageW(training_guard,CB_GETCURSEL,0,0);
    option_meter=(unsigned)SendMessageW(training_meter,CB_GETCURSEL,0,0);
    option_regen=SendMessageW(training_regen,BM_GETCHECK,0,0)==BST_CHECKED;
    option_train_stats=SendMessageW(training_stats,BM_GETCHECK,0,0)==BST_CHECKED;
    option_train_inputs=SendMessageW(training_inputs,BM_GETCHECK,0,0)==BST_CHECKED;
    option_hitboxes=SendMessageW(training_hitboxes,BM_GETCHECK,0,0)==BST_CHECKED;
    option_frame_meter=SendMessageW(training_meter_frames,BM_GETCHECK,0,0)==BST_CHECKED;
    training_record_mode=0;options_save_training();if(option_training)options_apply_training(training_context);
}
static void training_restart(unsigned enabled){
    training_read_options();options_save_mode(0,enabled);options_save_training();
    host_restart=1;DestroyWindow(host_window);
}
static LRESULT CALLBACK training_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_COMMAND){unsigned id=LOWORD(wp);
        if(id>=205&&id<=208){training_read_options();training_record_action(id);DestroyWindow(w);return 0;}
        if(id==201){training_restart(1);return 0;}
        if(id==202){training_restart(0);return 0;}
        if(id==203){training_read_options();DestroyWindow(w);return 0;}
        if(id==204){training_read_options();training_reset_requested=1;DestroyWindow(w);return 0;}
    }
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}
    if(msg==WM_DESTROY){training_window=NULL;SetForegroundWindow(host_window);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
static HWND training_combo(const wchar_t *label,int y,const wchar_t **names,unsigned count,unsigned selected){
    HINSTANCE inst=GetModuleHandle(NULL);
    CreateWindowW(L"STATIC",label,WS_CHILD|WS_VISIBLE,20,y+4,200,24,training_window,NULL,inst,NULL);
    HWND combo=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,228,y,310,200,training_window,NULL,inst,NULL);
    for(unsigned i=0;i<count;i++)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)names[i]);
    SendMessageW(combo,CB_SETCURSEL,selected,0);return combo;
}
static HWND training_check(const wchar_t *label,int y,unsigned checked){
    HWND c=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,20,y,510,26,training_window,NULL,GetModuleHandle(NULL),NULL);
    SendMessageW(c,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);return c;
}
static void training_open(void){
    if(training_window){SetForegroundWindow(training_window);return;}
    WNDCLASSW wc={0};wc.lpfnWndProc=training_proc;wc.hInstance=GetModuleHandle(NULL);wc.lpszClassName=L"UpperTrainingOptions";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    training_window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Configurar treino",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,640,665,host_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",option_training?L"Modo atual: treino":option_versus?L"Modo atual: versus local (P1 x P2)":L"Modo atual: arcade",WS_CHILD|WS_VISIBLE,20,16,520,25,training_window,NULL,wc.hInstance,NULL);
    const wchar_t *dummy[]={L"Em p\u00e9",L"Agachado",L"Pulando",L"CPU (dificuldade do menu Partida)",L"Controle do jogador 2"};
    const wchar_t *guard[]={L"Sem defesa",L"Defender ap\u00f3s o primeiro golpe",L"Sempre defender"};
    const wchar_t *meter[]={L"Normal",L"Recarregar automaticamente",L"Sempre cheia"};
    training_dummy=training_combo(L"Advers\u00e1rio",54,dummy,5,option_dummy);
    training_guard=training_combo(L"Defesa do advers\u00e1rio",92,guard,3,option_guard);
    training_meter=training_combo(L"Barra especial (ambos)",130,meter,3,option_meter);
    training_regen=training_check(L"Recuperar a vida dos dois personagens",174,option_regen);
    training_stats=training_check(L"Mostrar dano, dano total, combo e atordoamento",208,option_train_stats);
    training_inputs=training_check(L"Mostrar hist\u00f3rico de comandos",242,option_train_inputs);
    training_hitboxes=training_check(L"Mostrar hitboxes (caixas de ataque e de colis\u00e3o)",276,option_hitboxes);
    training_meter_frames=training_check(L"Mostrar medidor de quadros e vantagem (P1 / P2)",310,option_frame_meter);
    CreateWindowW(L"STATIC",L"Escolha os dois personagens com P1. Na grava\u00e7\u00e3o, P1 controla o dummy.\nGrava\u00e7\u00e3o nesta sess\u00e3o (at\u00e9 3.600 quadros). Tempo infinito.",WS_CHILD|WS_VISIBLE,20,346,590,42,training_window,NULL,wc.hInstance,NULL);
    const wchar_t *buttons[]={L"Iniciar treino",L"Voltar ao arcade",L"Aplicar e fechar",L"Reposicionar"};
    for(unsigned i=0;i<4;i++){HWND b=CreateWindowW(L"BUTTON",buttons[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP,20+(i%2)*268,398+(i/2)*34,252,29,training_window,(HMENU)(UINT_PTR)(201+i),wc.hInstance,NULL);if(i==3)EnableWindow(b,training_in_battle());}
    const wchar_t *record_buttons[]={L"Gravar dummy com P1",L"Parar grava\u00e7\u00e3o / reprodu\u00e7\u00e3o",L"Reproduzir em loop",L"Limpar grava\u00e7\u00e3o"};
    for(unsigned i=0;i<4;i++){HWND b=CreateWindowW(L"BUTTON",record_buttons[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP,20+(i%2)*268,470+(i/2)*34,252,29,training_window,(HMENU)(UINT_PTR)(205+i),wc.hInstance,NULL);EnableWindow(b,training_in_battle()&&(i<2||*(int *)(training_context+0xca8)>0));}
    wchar_t status[128];swprintf(status,128,L"Dummy: %ls | %d quadros gravados",training_record_mode==1?L"gravando":training_record_mode==2?L"reproduzindo":L"parado",training_context?*(int *)(training_context+0xca8):0);
    CreateWindowW(L"STATIC",status,WS_CHILD|WS_VISIBLE,20,546,590,24,training_window,NULL,wc.hInstance,NULL);
    CreateWindowW(L"STATIC",L"Verde: in\u00edcio | Vermelho: ativo | Azul: recupera\u00e7\u00e3o\nAmarelo/laranja: rea\u00e7\u00e3o/defesa | Cinza: hitstop | Ciano: livre\nVantagem positiva: voc\u00ea pode agir antes do advers\u00e1rio.",WS_CHILD|WS_VISIBLE,20,576,590,56,training_window,NULL,wc.hInstance,NULL);
    ShowWindow(training_window,SW_SHOW);SetForegroundWindow(training_window);
}
static void training_record_action(unsigned action){
    if(!training_in_battle())return;
    training_record_mode=0;options_apply_training(training_context);
    if(action==205){
        *(int *)(training_context+0xca8)=0;*(int *)(training_context+0xcac)=0;
        training_record_mode=1;training_reset_requested=1;*(unsigned *)(training_context+0x850)=3;
    }else if(action==207&&*(int *)(training_context+0xca8)>0){
        training_record_mode=2;training_reset_requested=1;*(unsigned *)(training_context+0x850)=4;
    }else if(action==208){*(int *)(training_context+0xca8)=0;*(int *)(training_context+0xcac)=0;}
}
static void training_before_tick(unsigned *p1,unsigned *p2){
    if(!option_training)return;
    if(training_reset_requested){
        training_reset_requested=0;
        if(training_in_battle()){
            uintptr_t reset=*(uintptr_t *)(training_context+0x28);
            frame_suspend=1;((void (*)(void))reset)();frame_suspend=0;frame_reset();
            memset(training_context+0x8d4,0,0x80);memset(training_context+0x984,0,0x18);
            memset(training_history,0,sizeof(training_history));
            *(int *)(training_context+0xcac)=0;if(training_record_mode==1)*(int *)(training_context+0xca8)=0;
        }
    }
    if(training_in_battle()){
        if(training_record_mode==1){*p2|=*p1;*p1=0;}
        else if(training_record_mode==2)*p2=0;
        else if(option_dummy==1)*p2=0x40;else if(option_dummy==2)*p2=0x10;
        else if(option_dummy!=4)*p2=0;
    }
    /* Finish the P1 character/ISM/speed choice, release, then choose the dummy.
       During the fight the two input profiles remain fully independent. */
    if(*(unsigned short *)(training_state+0x28)==1&&*(unsigned short *)(training_state+0x2a)==1&&training_state[0x1fc]==4){
        if(!*p1)training_select_armed=1;
        if(training_select_armed)*p2|=*p1;
    }else training_select_armed=0;
}
static void training_after_tick(unsigned p1,unsigned p2){
    if(!training_in_battle())return;
    unsigned bits[]={p1&0xfaf0,p2&0xfaf0};
    if(training_record_mode==2){
        unsigned raw=*(unsigned short *)(training_state+0x778+0x1f2);const unsigned *map=(const unsigned *)(uintptr_t)UINT64_C(0x1411f7e50);bits[1]=0;
        for(unsigned i=0;i<12;i++)if(raw&map[i*2+1])bits[1]|=map[i*2];bits[1]&=0xfaf0;
    }
    if(training_record_mode==1&&*(int *)(training_context+0xca8)>=3600){training_record_mode=0;options_apply_training(training_context);}
    for(unsigned p=0;p<2;p++){
        TrainingInput *h=training_history[p];
        if(h[0].frames&&h[0].bits==bits[p]){if(h[0].frames<9999)h[0].frames++;}
        else{memmove(h+1,h,6*sizeof(*h));h[0].bits=bits[p];h[0].frames=1;}
    }
}
static void training_input_label(unsigned bits,wchar_t *label,size_t n){
    label[0]=0;unsigned d=bits&0xf0;
    const wchar_t *arrow=d==0x90?L"\u2196":d==0x30?L"\u2197":d==0xc0?L"\u2199":d==0x60?L"\u2198":d==0x10?L"\u2191":d==0x40?L"\u2193":d==0x80?L"\u2190":d==0x20?L"\u2192":L"";
    wcsncpy(label,arrow,n-1);label[n-1]=0;
    const unsigned masks[]={0x8000,0x1000,0x200,0x4000,0x2000,0x800};const wchar_t *names[]={L" LP",L" MP",L" HP",L" LK",L" MK",L" HK"};
    for(unsigned i=0;i<6;i++)if(bits&masks[i])wcsncat(label,names[i],n-wcslen(label)-1);
    if(!label[0])wcsncpy(label,L"\u00b7",n);
}
static void training_text(HDC dc,const wchar_t *text,RECT r,UINT flags,COLORREF color){
    RECT shadow=r;OffsetRect(&shadow,1,1);SetTextColor(dc,RGB(0,0,0));DrawTextW(dc,text,-1,&shadow,flags);SetTextColor(dc,color);DrawTextW(dc,text,-1,&r,flags);
}
static void frame_paint(HDC dc,RECT view,int unit){
    if(!option_frame_meter||!frame_length)return;
    static const COLORREF colors[]={RGB(42,46,60),RGB(60,200,110),RGB(240,60,60),RGB(70,120,250),RGB(245,205,65),RGB(245,150,55),RGB(125,95,170),RGB(125,130,140),RGB(55,225,225)};
    int width=view.right-view.left,cell=(width-40)/FRAME_CELLS;if(cell<2)cell=2;if(cell>12)cell=12;
    int start=view.left+(width-cell*FRAME_CELLS)/2,grid_y=view.bottom-unit*2;
    int row_height=unit*2/3;if(row_height<7)row_height=7;
    RECT panel={view.left,view.bottom-unit*5,view.right,view.bottom};FillRect(dc,&panel,(HBRUSH)GetStockObject(BLACK_BRUSH));
    for(unsigned p=0;p<2;p++){
        FrameTrack *t=&frame_tracks[p];int x=p?view.left+width/2+8:view.left+12;
        wchar_t line[160];
        if(t->in_move&&t->had_active)swprintf(line,160,L"%uP  In\u00edcio %u   Ativos %u   Recupera\u00e7\u00e3o %u",p+1,t->first_active,t->active,t->total-t->last_active);
        else if(t->in_move)swprintf(line,160,L"%uP  In\u00edcio %u   Ativos --   Recupera\u00e7\u00e3o --",p+1,t->total+1);
        else if(t->shown_at)swprintf(line,160,L"%uP  In\u00edcio %u   Ativos %u   Recupera\u00e7\u00e3o %u",p+1,t->shown_start,t->shown_active,t->shown_recovery);
        else swprintf(line,160,L"%uP  In\u00edcio --   Ativos --   Recupera\u00e7\u00e3o --",p+1);
        RECT text={x,view.bottom-unit*5+2,p?view.right-8:view.left+width/2-8,view.bottom-unit*4+3};training_text(dc,line,text,DT_LEFT|DT_SINGLELINE|DT_VCENTER,RGB(255,255,255));
        unsigned other=frame_tracks[1-p].doing;
        if(t->doing==FRAME_FREE&&(other==FRAME_HIT||other==FRAME_GUARD))swprintf(line,160,L"LIVRE: pode iniciar outro golpe");
        else if(frame_advantage_player==(int)p)swprintf(line,160,L"%ls: %+d quadros",frame_advantage_guarded?L"Na defesa":L"No acerto",frame_advantage);
        else line[0]=0;
        text.top=view.bottom-unit*4+3;text.bottom=view.bottom-unit*3+4;training_text(dc,line,text,DT_LEFT|DT_SINGLELINE|DT_VCENTER,RGB(80,235,225));
        for(unsigned n=0;n<FRAME_CELLS;n++){
            unsigned kind=n<frame_length?frame_cells[p][n]:FRAME_FREE;RECT box={start+(int)n*cell,grid_y+(int)p*(row_height+3),start+((int)n+1)*cell-1,grid_y+(int)p*(row_height+3)+row_height};
            SetDCBrushColor(dc,colors[kind<9?kind:0]);FillRect(dc,&box,(HBRUSH)GetStockObject(DC_BRUSH));
        }
        wchar_t label[4];swprintf(label,4,L"%u",p+1);RECT pos={view.left+2,grid_y+(int)p*(row_height+3)-2,start-2,grid_y+(int)p*(row_height+3)+row_height+2};training_text(dc,label,pos,DT_LEFT|DT_SINGLELINE|DT_VCENTER,RGB(255,255,255));
    }
}
static void training_paint(HDC dc,RECT view){
    if(!training_in_battle())return;
    int height=view.bottom-view.top,unit=height/34;if(unit<12)unit=12;if(unit>36)unit=36;
    HFONT font=CreateFontW(-unit,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");HGDIOBJ old=SelectObject(dc,font);
    int old_mode=SetBkMode(dc,TRANSPARENT);COLORREF old_color=SetTextColor(dc,RGB(255,255,255));
    if(training_record_mode){wchar_t mode[128];swprintf(mode,128,training_record_mode==1?L"GRAVANDO DUMMY (P1): %d":L"DUMMY EM LOOP (%d quadros)",*(int *)(training_context+0xca8));RECT status={view.left+unit,view.top+height/5,view.right-unit,view.top+height/5+unit*2};training_text(dc,mode,status,DT_LEFT|DT_SINGLELINE|DT_VCENTER,training_record_mode==1?RGB(255,110,80):RGB(110,255,150));}
    if(option_train_stats){
        const wchar_t *labels[]={L"Dano",L"Dano total",L"Combo",L"Atordoamento"};const unsigned offsets[]={0x8d4,0x914,0x944,0x994};
        int width=unit*17,x=view.right-width-unit,y=view.top+height/5;
        
        for(unsigned i=0;i<4;i++){wchar_t value[64];int a=*(int *)(training_context+offsets[i]),b=*(int *)(training_context+offsets[i]+4);swprintf(value,64,L"%d / %d",a,b);
            RECT l={x,y+(int)i*unit*3/2,x+unit*11,y+(int)i*unit*3/2+unit*3/2};training_text(dc,labels[i],l,DT_LEFT|DT_SINGLELINE|DT_VCENTER,RGB(255,155,64));
            l.left=x+unit*11;l.right=view.right-unit;training_text(dc,value,l,DT_RIGHT|DT_SINGLELINE|DT_VCENTER,RGB(255,255,255));
        }
    }
    if(option_train_inputs)for(unsigned p=0;p<(option_dummy==4||training_record_mode?2u:1u);p++){
        int x=p?view.right-unit*15:view.left+unit,y=view.bottom-unit*(option_frame_meter?16:11);
        
        for(int i=6;i>=0;i--)if(training_history[p][i].frames){wchar_t input[96],line[128];training_input_label(training_history[p][i].bits,input,96);swprintf(line,128,L"%4u  %ls",training_history[p][i].frames,input);RECT r={x,y+(6-i)*unit*5/4,x+unit*14,y+(7-i)*unit*5/4};training_text(dc,line,r,DT_LEFT|DT_SINGLELINE|DT_VCENTER,RGB(255,255,255));}
    }
    frame_paint(dc,view,unit);
    SetTextColor(dc,old_color);SetBkMode(dc,old_mode);SelectObject(dc,old);DeleteObject(font);
}
