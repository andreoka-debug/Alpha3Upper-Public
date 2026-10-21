/* Presentation only: original game renders 384x224; letterboxing never crops it. */
static HMENU host_menu,aspect_menu,filter_menu,display_menu;
static WINDOWPLACEMENT windowed_placement={sizeof(WINDOWPLACEMENT)};
static int fullscreen_active;
static uint32_t crt_pixels[SCREEN_W*2*SCREEN_H*2];
static unsigned char crt_gain[3][SCREEN_W*2*SCREEN_H*2];
static unsigned crt_cached=99;
static RECT display_viewport(int width,int height,unsigned wide){
    RECT r={0,0,width,height};int a=wide?16:4,b=wide?9:3;
    if((int64_t)width*b>(int64_t)height*a){r.right=(int)((int64_t)height*a/b);r.left=(width-r.right)/2;r.right+=r.left;}
    else{r.bottom=(int)((int64_t)width*b/a);r.top=(height-r.bottom)/2;r.bottom+=r.top;}return r;
}
static void crt_prepare(void){
    if(crt_cached==option_crt)return;crt_cached=option_crt;
    for(int y=0;y<SCREEN_H*2;y++)for(int x=0;x<SCREEN_W*2;x++){
        float scan=(y&1)?(option_crt==4?.73f:option_crt==1?.68f:option_crt==2?.78f:.58f):1.f;
        float nx=(x-SCREEN_W)/(float)SCREEN_W,ny=(y-SCREEN_H)/(float)SCREEN_H;
        float vignette=option_crt>=2?1.f-(option_crt==4?.055f:option_crt==2?.09f:.16f)*(nx*nx+ny*ny):1.f;
        for(int c=0;c<3;c++){int phase=option_crt==4?(x+(y/2%2))%3:x%3;float mask=option_crt>=2&&phase!=c?(option_crt==4?.79f:option_crt==2?.90f:.76f):1.f;crt_gain[c][y*SCREEN_W*2+x]=(unsigned char)(255*scan*mask*vignette);}
    }
}
static void crt_render(void){
    crt_prepare();for(int y=0;y<SCREEN_H*2;y++)for(int x=0;x<SCREEN_W*2;x++){
        unsigned p=y*SCREEN_W*2+x,v=screen_pixels[(y/2)*SCREEN_W+x/2];
        /* Independently implemented Type-B-inspired presentation: short
         * phosphor triads, staggered rows and a small horizontal beam spread.
         * This is an approximation, not the collection's proprietary shader. */
        if(option_crt==4){unsigned sx=x/2,sy=y/2,nx=(x&1)?(sx+1<SCREEN_W?sx+1:sx):(sx?sx-1:sx);unsigned neighbor=screen_pixels[sy*SCREEN_W+nx];
            unsigned rr=((((v>>16)&255)*15+((neighbor>>16)&255))>>4),gg=((((v>>8)&255)*15+((neighbor>>8)&255))>>4),bb=(((v&255)*15+(neighbor&255))>>4);v=(rr<<16)|(gg<<8)|bb;
        }
        unsigned r=((v>>16)&255)*crt_gain[0][p]/255,g=((v>>8)&255)*crt_gain[1][p]/255,b=(v&255)*crt_gain[2][p]/255;
        crt_pixels[p]=(r<<16)|(g<<8)|b;
    }
}
/* Compose the entire client area in memory. Never expose the black clear between frames. */
static HDC present_dc;
static HBITMAP present_bitmap;
static HGDIOBJ present_original;
static int present_width,present_height;
static void display_release(void){
    if(present_dc){if(present_original)SelectObject(present_dc,present_original);if(present_bitmap)DeleteObject(present_bitmap);DeleteDC(present_dc);}
    present_dc=NULL;present_bitmap=NULL;present_original=NULL;present_width=present_height=0;
}
static int display_surface(HDC target,int width,int height){
    if(!present_dc){present_dc=CreateCompatibleDC(target);if(!present_dc)return 0;}
    if(width!=present_width||height!=present_height){
        HBITMAP replacement=CreateCompatibleBitmap(target,width,height);if(!replacement)return 0;
        HGDIOBJ previous=SelectObject(present_dc,replacement);
        if(!previous||previous==HGDI_ERROR){DeleteObject(replacement);return 0;}
        if(present_bitmap)DeleteObject(present_bitmap);else present_original=previous;
        present_bitmap=replacement;present_width=width;present_height=height;
    }return 1;
}
static void paint_frame(HDC target){
    RECT client;GetClientRect(host_window,&client);
    if(client.right<=0||client.bottom<=0||!display_surface(target,client.right,client.bottom))return;
    RECT view=display_viewport(client.right,client.bottom,option_aspect);
    if(view.right<=view.left||view.bottom<=view.top)return;
    FillRect(present_dc,&client,(HBRUSH)GetStockObject(BLACK_BRUSH));
    BITMAPINFO bi=bitmap_info();const void *pixels=screen_pixels;
    if(option_crt){crt_render();bi.bmiHeader.biWidth=SCREEN_W*2;bi.bmiHeader.biHeight=-SCREEN_H*2;pixels=crt_pixels;}
    SetStretchBltMode(present_dc,COLORONCOLOR);
    StretchDIBits(present_dc,view.left,view.top,view.right-view.left,view.bottom-view.top,0,0,bi.bmiHeader.biWidth,-bi.bmiHeader.biHeight,pixels,&bi,DIB_RGB_COLORS,SRCCOPY);
    training_paint(present_dc,view);
    versus_paint(present_dc,view);
    performance_paint(present_dc,view);
    online_paint(present_dc,view);
    /* Only the finished picture and its borders reach the visible window. */
    BitBlt(target,0,0,client.right,client.bottom,present_dc,0,0,SRCCOPY);
}
static void display_update_menu(void){
    CheckMenuItem(display_menu,4700,MF_BYCOMMAND|(option_fullscreen?MF_CHECKED:MF_UNCHECKED));
    CheckMenuRadioItem(aspect_menu,4710,4711,4710+option_aspect,MF_BYCOMMAND);
    CheckMenuRadioItem(filter_menu,4720,4724,4720+option_crt,MF_BYCOMMAND);
}
static void display_fullscreen(unsigned enabled){
    if(enabled&&!fullscreen_active){
        GetWindowPlacement(host_window,&windowed_placement);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(host_window,MONITOR_DEFAULTTONEAREST),&mi);
        fullscreen_active=1;SetMenu(host_window,NULL);SetWindowLongPtrW(host_window,GWL_STYLE,WS_POPUP|WS_VISIBLE);
        SetWindowPos(host_window,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER);
    }else if(!enabled&&fullscreen_active){fullscreen_active=0;SetWindowLongPtrW(host_window,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetMenu(host_window,host_menu);SetWindowPlacement(host_window,&windowed_placement);SetWindowPos(host_window,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}
    option_fullscreen=enabled;options_save_display();display_update_menu();InvalidateRect(host_window,NULL,FALSE);
}
static void display_add_menu(HMENU bar){
    host_menu=bar;display_menu=CreatePopupMenu();aspect_menu=CreatePopupMenu();filter_menu=CreatePopupMenu();
    AppendMenuW(display_menu,MF_STRING,4700,L"Tela cheia\tF11 / Alt+Enter");
    AppendMenuW(aspect_menu,MF_STRING,4710,L"4:3 (cl\u00e1ssico)");AppendMenuW(aspect_menu,MF_STRING,4711,L"16:9 (wide, imagem esticada)");
    AppendMenuW(display_menu,MF_POPUP,(UINT_PTR)aspect_menu,L"Propor\u00e7\u00e3o");
    const wchar_t *names[]={L"Desligado",L"Scanlines",L"CRT suave",L"CRT intenso",L"CRT tipo B (aproxima\u00e7\u00e3o)"};for(unsigned i=0;i<5;i++)AppendMenuW(filter_menu,MF_STRING,4720+i,names[i]);
    AppendMenuW(display_menu,MF_POPUP,(UINT_PTR)filter_menu,L"Filtro CRT");
    AppendMenuW(display_menu,MF_STRING|MF_GRAYED,0,L"F10: mostrar/ocultar menu em tela cheia");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)display_menu,L"Tela");display_update_menu();
}
