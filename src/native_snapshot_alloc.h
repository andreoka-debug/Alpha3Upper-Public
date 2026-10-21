/* Only the original core and its synchronous frontend buffers are watched.
   Networking, audio workers, window state and resources owned by WebRTC are not. */
#ifdef UPPER_ROLLBACK
typedef struct {unsigned char *base,*shadow;size_t size;} SnapRegion;
static SnapRegion snap_regions[8];static unsigned snap_region_count;
static void *snap_allocate(uintptr_t address,size_t size){
 size=(size+4095)&~(size_t)4095;
 if(snap_region_count==8)return NULL;
 void *p=VirtualAlloc((void *)address,size,MEM_RESERVE|MEM_COMMIT|MEM_WRITE_WATCH,PAGE_READWRITE);
 if(p)snap_regions[snap_region_count++]=(SnapRegion){p,NULL,size};return p;
}
static int rollback_texture_keep(unsigned id);
#else
static void *snap_allocate(uintptr_t address,size_t size){return address?VirtualAlloc((void *)address,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE):calloc(1,size);}
#endif
