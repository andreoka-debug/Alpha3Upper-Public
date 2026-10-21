/* Passive CPU-side telemetry. Does not change input, simulation or pacing.
   A completed GDI submission is not a measured monitor presentation. */
#define PERFORMANCE_WINDOW 240
#define PERFORMANCE_TARGET_MS (1000.0/60.0)
#define PERFORMANCE_LATE_MS (PERFORMANCE_TARGET_MS+2.0)
typedef struct {double interval,cpu,input,simulation,render,audio,present,read_to_submit;} PerformanceSample;
typedef struct {unsigned count;double mean,p95,p99,deviation,minimum,maximum,cpu,read_to_submit;} PerformanceSummary;
static PerformanceSample performance_samples[PERFORMANCE_WINDOW];
static unsigned performance_head,performance_count;
static unsigned long long performance_frames,performance_intervals,performance_late,performance_battle_frames;
static double performance_sum,performance_square_sum,performance_minimum,performance_maximum,performance_cpu_sum,performance_read_sum;
static LONGLONG performance_frequency,performance_origin,performance_previous,performance_marks[5],performance_last_refresh;
static int performance_frame_active,performance_dirty=1;
static int performance_io_error;
static PerformanceSummary performance_cached;
static FILE *performance_csv;
static char performance_csv_path[MAX_PATH],performance_report_path[MAX_PATH],performance_buffer[65536];
static HMENU performance_menu;
static HFONT performance_font;
static int performance_font_size;
static unsigned performance_path_serial;
static LONGLONG performance_now(void){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
static void performance_init_clock(void){
    if(!performance_frequency){LARGE_INTEGER f;QueryPerformanceFrequency(&f);performance_frequency=f.QuadPart;performance_origin=performance_now();}
}
static double performance_ms(LONGLONG ticks){return 1000.0*(double)ticks/(double)performance_frequency;}
static int performance_enabled(void){return option_performance||performance_csv!=NULL;}
static void performance_suspend(void){performance_previous=0;performance_frame_active=0;}
static void performance_reset(void){
    memset(performance_samples,0,sizeof(performance_samples));memset(&performance_cached,0,sizeof(performance_cached));
    performance_head=performance_count=0;performance_frames=performance_intervals=performance_late=performance_battle_frames=0;
    performance_sum=performance_square_sum=performance_minimum=performance_maximum=performance_cpu_sum=performance_read_sum=0;
    performance_dirty=1;performance_last_refresh=0;performance_suspend();
}
static int performance_compare(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static PerformanceSummary performance_summary(void){
    PerformanceSummary r={0};double values[PERFORMANCE_WINDOW],sum=0,squares=0;
    for(unsigned i=0;i<performance_count;i++){
        PerformanceSample *s=&performance_samples[i];r.cpu+=s->cpu;r.read_to_submit+=s->read_to_submit;
        if(s->interval>0){values[r.count++]=s->interval;sum+=s->interval;squares+=s->interval*s->interval;}
    }
    if(performance_count){r.cpu/=performance_count;r.read_to_submit/=performance_count;}
    if(r.count){
        qsort(values,r.count,sizeof(double),performance_compare);r.mean=sum/r.count;
        r.minimum=values[0];r.maximum=values[r.count-1];
        r.p95=values[(95*r.count+99)/100-1];r.p99=values[(99*r.count+99)/100-1];
        double variance=squares/r.count-r.mean*r.mean;r.deviation=sqrt(variance>0?variance:0);
    }return r;
}
static void performance_accept(PerformanceSample sample,unsigned char *state,LONGLONG ended){
    performance_samples[performance_head]=sample;performance_head=(performance_head+1)%PERFORMANCE_WINDOW;
    if(performance_count<PERFORMANCE_WINDOW)performance_count++;
    performance_frames++;performance_cpu_sum+=sample.cpu;performance_read_sum+=sample.read_to_submit;performance_dirty=1;
    unsigned battle=state&&*(unsigned short *)(state+0x28)==1&&*(unsigned short *)(state+0x2a)==2&&
        *(unsigned short *)(state+0x2c)==3&&*(unsigned short *)(state+0x2e)==3;
    performance_battle_frames+=battle;
    if(sample.interval>0){
        if(!performance_intervals||sample.interval<performance_minimum)performance_minimum=sample.interval;
        performance_intervals++;performance_sum+=sample.interval;performance_square_sum+=sample.interval*sample.interval;
        if(sample.interval>performance_maximum)performance_maximum=sample.interval;
        if(sample.interval>PERFORMANCE_LATE_MS)performance_late++;
    }
    if(performance_csv){
        fprintf(performance_csv,"%llu,%.3f,",performance_frames,performance_ms(ended-performance_origin));
        if(sample.interval>0)fprintf(performance_csv,"%.6f",sample.interval);
        fprintf(performance_csv,",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%s,%u,%u,%u\n",
            sample.input,sample.simulation,sample.render,sample.audio,sample.present,sample.cpu,sample.read_to_submit,
            sample.interval>PERFORMANCE_TARGET_MS?sample.interval-PERFORMANCE_TARGET_MS:0,
            sample.interval>PERFORMANCE_LATE_MS,battle,option_training?"training":option_versus?"versus":"arcade",
            state?state[0x60]:0,option_crt,option_fullscreen);
        if(performance_frames%120==0)fflush(performance_csv);
        if(ferror(performance_csv)){fclose(performance_csv);performance_csv=NULL;performance_io_error=1;}
    }
}
static void performance_begin(void){
    if(!performance_enabled())return;
    performance_init_clock();
    if(host_self_test||IsIconic(host_window)||GetForegroundWindow()!=host_window){performance_suspend();return;}
    performance_frame_active=1;performance_marks[0]=performance_now();
}
static void performance_mark(unsigned stage){if(performance_frame_active&&stage<5)performance_marks[stage]=performance_now();}
static void performance_finish_at(LONGLONG ended,unsigned char *state){
    if(!performance_frame_active)return;
    PerformanceSample s={0};
    if(performance_previous)s.interval=performance_ms(ended-performance_previous);
    s.input=performance_ms(performance_marks[1]-performance_marks[0]);
    s.simulation=performance_ms(performance_marks[2]-performance_marks[1]);
    s.render=performance_ms(performance_marks[3]-performance_marks[2]);
    s.audio=performance_ms(performance_marks[4]-performance_marks[3]);
    s.present=performance_ms(ended-performance_marks[4]);s.cpu=performance_ms(ended-performance_marks[0]);
    s.read_to_submit=performance_ms(ended-performance_marks[1]);
    performance_previous=ended;performance_frame_active=0;performance_accept(s,state,ended);
}
static void performance_finish(unsigned char *state){if(performance_frame_active)performance_finish_at(performance_now(),state);}
static int performance_new_path(char *path,const char *extension){
    if(!CreateDirectoryA("data/diagnosticos",NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    SYSTEMTIME t;GetLocalTime(&t);
    snprintf(path,MAX_PATH,"data/diagnosticos/desempenho-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%u.%s",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId(),++performance_path_serial,extension);return 1;
}
static int performance_write_report(const char *path){
    FILE *f=fopen(path,"w");if(!f)return 0;
    double mean=performance_intervals?performance_sum/performance_intervals:0;
    double variance=performance_intervals?performance_square_sum/performance_intervals-mean*mean:0;
    PerformanceSummary recent=performance_summary();
    fprintf(f,"ALPHA 3 UPPER 0.11.1 - DIAGNOSTICO INTERNO\n\n");
    fprintf(f,"Escopo: amostras desde o ultimo inicio/zeramento da medicao.\nQuadros medidos: %llu\nQuadros em luta ativa: %llu\nIntervalos validos: %llu\n",performance_frames,performance_battle_frames,performance_intervals);
    fprintf(f,"Alvo do host: %.6f ms (60 envios/s)\nMedia de intervalo: %.6f ms\nEnvios/s pela media: %.3f\nMinimo: %.6f ms\nMaximo: %.6f ms\nOscilacao (desvio padrao): %.6f ms\nIntervalos acima de %.6f ms: %llu\n",
        PERFORMANCE_TARGET_MS,mean,mean>0?1000/mean:0,performance_minimum,performance_maximum,sqrt(variance>0?variance:0),PERFORMANCE_LATE_MS,performance_late);
    fprintf(f,"CPU media por quadro: %.6f ms\nLeitura de comandos -> fim do envio (CPU), media: %.6f ms\n",
        performance_frames?performance_cpu_sum/performance_frames:0,performance_frames?performance_read_sum/performance_frames:0);
    fprintf(f,"\nJanela recente: ate %u quadros medidos; %u intervalos validos\nP95 recente: %.6f ms\nP99 recente: %.6f ms\n",PERFORMANCE_WINDOW,recent.count,recent.p95,recent.p99);
    fprintf(f,"\nIntervalos comparam o fim de dois envios completos pela CPU.\nO primeiro quadro e o primeiro apos uma interrupcao nao tem intervalo.\nPausas, menus, redimensionamento, segundo plano e minimizacao interrompem a amostra.\nJanelas de configuracao interrompem a amostra. Autotestes sem ritmo real nao sao medidos.\nCPU inclui leitura, jogo, desenho, audio e envio; exclui espera entre quadros.\nCSV tem escrita em disco e seu custo pode afetar intervalos seguintes.\nFPS/envios nao medem a atualizacao fisica do monitor.\nLeitura -> envio nao mede atraso fisico do controle, USB, DWM, VSync ou tela.\nContagens acima do limite nao equivalem a quadros perdidos comprovados.\nTurbo pode fazer mais de um passo de simulacao por envio. Nao se contam esses passos como novos envios.\nCSV usa milissegundos e ponto decimal; campo de intervalo vazio indica uma descontinuidade.\n");
    int ok=!ferror(f);if(fclose(f)!=0)ok=0;return ok;
}
static void performance_stop_recording(void){
    if(!performance_csv)return;
    int ok=fflush(performance_csv)==0;if(fclose(performance_csv)!=0)ok=0;performance_csv=NULL;
    if(!performance_write_report(performance_report_path)||!ok)performance_io_error=1;
}
static int performance_start_recording(void){
    if(!performance_new_path(performance_csv_path,"csv"))return 0;
    FILE *f=fopen(performance_csv_path,"w");if(!f)return 0;
    setvbuf(f,performance_buffer,_IOFBF,sizeof(performance_buffer));
    fputs("frame,session_elapsed_ms,interval_ms,input_poll_ms,simulation_ms,render_ms,audio_ms,submit_ms,cpu_total_ms,read_to_submit_ms,excess_over_16_667_ms,over_18_667_ms,active_battle,mode,turbo_raw,crt,fullscreen\n",f);
    if(ferror(f)){fclose(f);return 0;}
    strncpy(performance_report_path,performance_csv_path,MAX_PATH-1);char *ext=strrchr(performance_report_path,'.');if(ext)strcpy(ext,".txt");
    performance_reset();performance_init_clock();performance_origin=performance_now();performance_csv=f;return 1;
}
static void performance_update_menu(void){
    if(!performance_menu)return;
    CheckMenuItem(performance_menu,5000,MF_BYCOMMAND|(option_performance?MF_CHECKED:0));
    CheckMenuItem(performance_menu,5001,MF_BYCOMMAND|(performance_csv?MF_CHECKED:0));
    EnableMenuItem(performance_menu,5002,MF_BYCOMMAND|(performance_csv?MF_GRAYED:MF_ENABLED));
}
static void performance_check_error(void){
    if(!performance_io_error)return;
    performance_io_error=0;performance_update_menu();performance_suspend();
    MessageBoxW(host_window,L"A grava\u00e7\u00e3o do diagn\u00f3stico n\u00e3o p\u00f4de ser conclu\u00edda. Verifique o espa\u00e7o e o acesso \u00e0 pasta data\\diagnosticos.",L"Diagn\u00f3stico",MB_OK|MB_ICONERROR);
}
static void performance_command(unsigned id){
    if(id==5000){option_performance=!option_performance;WritePrivateProfileStringA("Diagnostico","Mostrar",option_performance?"1":"0",option_file);performance_suspend();}
    else if(id==5001){
        if(performance_csv)performance_stop_recording();
        else if(!performance_start_recording())MessageBoxW(host_window,L"N\u00e3o foi poss\u00edvel gravar em data\\diagnosticos. Verifique o acesso \u00e0 pasta do jogo.",L"Diagn\u00f3stico",MB_OK|MB_ICONERROR);
    }else if(id==5002&&!performance_csv)performance_reset();
    else if(id==5003){
        char path[MAX_PATH];
        if(performance_new_path(path,"txt")&&performance_write_report(path)){
            char full[MAX_PATH],message[MAX_PATH+80];GetFullPathNameA(path,MAX_PATH,full,NULL);snprintf(message,sizeof(message),"Relatorio salvo em:\n%s",full);MessageBoxA(host_window,message,"Diagnostico",MB_OK);
        }else MessageBoxW(host_window,L"N\u00e3o foi poss\u00edvel salvar o relat\u00f3rio.",L"Diagn\u00f3stico",MB_OK|MB_ICONERROR);
    }
    performance_update_menu();
    performance_check_error();
}
static void performance_add_menu(HMENU bar){
    performance_menu=CreatePopupMenu();
    AppendMenuW(performance_menu,MF_STRING,5000,L"Mostrar desempenho na tela");
    AppendMenuW(performance_menu,MF_STRING,5001,L"Gravar CSV (iniciar / parar)");
    AppendMenuW(performance_menu,MF_STRING,5002,L"Zerar medi\u00e7\u00e3o");
    AppendMenuW(performance_menu,MF_STRING,5003,L"Salvar relat\u00f3rio...");
    AppendMenuW(performance_menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(performance_menu,MF_STRING|MF_GRAYED,0,L"Alvo: 16,67 ms | Marca atrasos acima de 18,67 ms");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)performance_menu,L"Diagn\u00f3stico");performance_update_menu();
}
static void performance_paint(HDC dc,RECT view){
    if(!option_performance)return;
    performance_init_clock();LONGLONG now=performance_now();
    if(performance_dirty&&(!performance_last_refresh||now-performance_last_refresh>performance_frequency/4)){
        performance_cached=performance_summary();performance_dirty=0;performance_last_refresh=now;
    }
    PerformanceSummary s=performance_cached;int height=view.bottom-view.top,width=view.right-view.left;
    int size=height/44;if(size<10)size=10;if(size>20)size=20;
    if(!performance_font||performance_font_size!=size){
        if(performance_font)DeleteObject(performance_font);
        performance_font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Consolas");performance_font_size=size;
    }
    int line_height=size+4,panel_width=size*25;if(panel_width>width-12)panel_width=width-12;
    RECT panel={view.right-panel_width-6,view.top+6,view.right-6,view.top+6+line_height*7+36};
    if(panel.bottom>view.bottom-6)panel.bottom=view.bottom-6;
    FillRect(dc,&panel,(HBRUSH)GetStockObject(BLACK_BRUSH));int mode=SetBkMode(dc,TRANSPARENT);HGDIOBJ old=SelectObject(dc,performance_font);
    wchar_t lines[7][160];
    swprintf(lines[0],160,L"DESEMPENHO  |  CSV: %ls",performance_csv?L"gravando":L"desligado");
    swprintf(lines[1],160,L"Envios/s %5.1f | CPU %5.2f ms",s.mean>0?1000/s.mean:0,s.cpu);
    swprintf(lines[2],160,L"Quadro %5.2f | P95 %5.2f ms",s.mean,s.p95);
    swprintf(lines[3],160,L"Oscila %5.2f | Pico %5.2f ms",s.deviation,s.maximum);
    swprintf(lines[4],160,L"Leitura -> envio (CPU): %.2f ms",s.read_to_submit);
    swprintf(lines[5],160,L">18,67 ms: %llu | total %llu",performance_late,performance_frames);
    swprintf(lines[6],160,L"Interno: n\u00e3o mede controle/tela");
    for(unsigned i=0;i<7;i++){
        RECT r={panel.left+7,panel.top+3+(int)i*line_height,panel.right-5,panel.top+3+((int)i+1)*line_height};
        training_text(dc,lines[i],r,DT_LEFT|DT_SINGLELINE|DT_VCENTER,i==0?RGB(90,225,230):i==5?RGB(255,205,85):RGB(245,245,245));
    }
    RECT graph={panel.left+7,panel.top+line_height*7+5,panel.right-7,panel.bottom-5};
    if(graph.bottom>graph.top){
        int h=graph.bottom-graph.top,w=graph.right-graph.left,y=graph.bottom-(int)(h*PERFORMANCE_TARGET_MS/50.0);
        SetDCBrushColor(dc,RGB(100,100,100));RECT baseline={graph.left,y,graph.right,y+1};FillRect(dc,&baseline,(HBRUSH)GetStockObject(DC_BRUSH));
        unsigned n=performance_count<120?performance_count:120;
        for(unsigned i=0;i<n;i++){
            unsigned index=(performance_head+PERFORMANCE_WINDOW-n+i)%PERFORMANCE_WINDOW;double interval=performance_samples[index].interval;
            if(interval<=0)continue;int bar=(int)(h*fmin(interval,50.0)/50.0);if(bar<1)bar=1;
            RECT r={graph.left+(int)i*w/120,graph.bottom-bar,graph.left+((int)i+1)*w/120-1,graph.bottom};
            SetDCBrushColor(dc,interval>PERFORMANCE_LATE_MS?RGB(245,110,70):RGB(70,190,135));FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH));
        }
    }
    SelectObject(dc,old);SetBkMode(dc,mode);
}
static void performance_shutdown(void){performance_stop_recording();if(performance_font)DeleteObject(performance_font);performance_font=NULL;}
