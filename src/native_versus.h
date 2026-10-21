/* Local two-player mode uses the collection's original versus entry.
   The collection frontend normally handles the result screen. */
static int versus_result_seen,versus_confirm_armed;
static int versus_result_active(void){
    if(!option_versus||option_training||!training_context||!training_state)return 0;
    unsigned phase=*(unsigned short *)(training_state+0x2a);
    return *(unsigned short *)(training_state+0x28)==1&&
        ((phase==2&&*(unsigned short *)(training_state+0x2c)==4&&
          *(unsigned *)(training_context+0x538)!=0)||phase==3||phase==4);
}
static void versus_new_selection(void){
    if(!option_versus||option_training||!training_context)return;
    /* Reset through the original scheduler's boot entry. Calling dfb990
       directly between frames uses the stale current-task pointer (slot 7),
       scheduling another game-control task alongside slot 0 and doubling
       simulation on subsequent matches. Rebuild the task table instead. */
    *(unsigned *)(training_context+0x218)=0;*(unsigned *)(training_context+0x21c)=0;
    ((void(*)(void))(uintptr_t)UINT64_C(0x140dcba30))();
    ((void(*)(void))(uintptr_t)UINT64_C(0x140dcd460))();
    ((void(*)(unsigned,unsigned))(uintptr_t)UINT64_C(0x140dcd6a0))(0,0);
    frame_reset();versus_result_seen=0;versus_confirm_armed=0;
}
static void versus_before_tick(unsigned *p1,unsigned *p2){
    if(!versus_result_active()){versus_result_seen=0;versus_confirm_armed=0;return;}
    if(!versus_result_seen){versus_result_seen=1;versus_confirm_armed=0;}
    unsigned confirm=(*p1|*p2)&0xfa08;
    *p1=0;*p2=0;
    if(!confirm)versus_confirm_armed=1;
    else if(versus_confirm_armed)versus_new_selection();
}
static void versus_paint(HDC dc,RECT view){
    if(!versus_result_active())return;
    int width=view.right-view.left,height=view.bottom-view.top;
    RECT panel={view.left+width/16,view.bottom-height/4,view.right-width/16,view.bottom-height/32};
    FillRect(dc,&panel,(HBRUSH)GetStockObject(BLACK_BRUSH));
    int old_mode=SetBkMode(dc,TRANSPARENT);
    int font_size=height/30;if(font_size<13)font_size=13;
    HFONT font=CreateFontW(-font_size,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,font);
    RECT line=panel;line.top+=font_size/2;line.bottom=line.top+font_size*2;
    training_text(dc,L"Versus local - partida encerrada",line,DT_CENTER|DT_SINGLELINE|DT_VCENTER,RGB(255,225,80));
    line.top=line.bottom;line.bottom=panel.bottom;
    training_text(dc,L"Start ou golpe (P1 / P2): selecionar personagens",line,DT_CENTER|DT_SINGLELINE|DT_VCENTER,RGB(255,255,255));
    SelectObject(dc,old);DeleteObject(font);SetBkMode(dc,old_mode);
}
