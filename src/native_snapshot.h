#ifdef UPPER_ROLLBACK
#define ROLLBACK_MAX 8
#define SNAP_SLOTS (ROLLBACK_MAX+2)
#define SNAP_MAX_PAGES 16384 /* 64 MiB of modified native pages per frame. */
typedef struct {uint32_t region,offset;unsigned char bytes[4096];} SnapPage;
typedef struct {unsigned id;HostTexture previous;} SnapTexture;
typedef struct {
 uint32_t frame;int valid;SnapPage *pages;size_t count,capacity;
 SnapTexture *textures;unsigned texture_count,texture_capacity;
 unsigned textures_before,palettes_before;int result_seen,confirm_armed;
} SnapFrame;
static SnapFrame snap_frames[SNAP_SLOTS],*snap_recording;
static void **snap_addresses;static int snap_initialized,snap_error;
static unsigned long long snap_captured_pages,snap_peak_pages,snap_rewinds;
static uint64_t snap_hash(const void *data,size_t size){const unsigned char *p=data;uint64_t h=UINT64_C(14695981039346656037);while(size--){h^=*p++;h*=UINT64_C(1099511628211);}return h;}
static void snap_discard(SnapFrame *s){
 for(unsigned i=0;i<s->texture_count;i++)free(s->textures[i].previous.pixels);
 s->texture_count=0;s->count=0;s->valid=0;
}
static int snap_init(void){
 if(snap_initialized)return 1;snap_addresses=malloc(65552*sizeof(void *));if(!snap_addresses)return 0;
 for(unsigned i=0;i<snap_region_count;i++){
  SnapRegion *r=snap_regions+i;r->shadow=VirtualAlloc(NULL,r->size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!r->shadow)return 0;
  memcpy(r->shadow,r->base,r->size);if(ResetWriteWatch(r->base,r->size))return 0;
 }
 snap_initialized=1;return 1;
}
static int snap_begin(uint32_t frame){
 SnapFrame *s=&snap_frames[frame%SNAP_SLOTS];snap_discard(s);s->frame=frame;s->valid=1;
 s->textures_before=captured_textures;s->palettes_before=captured_palettes;s->result_seen=versus_result_seen;s->confirm_armed=versus_confirm_armed;
 snap_recording=s;return !snap_error;
}
static int rollback_texture_keep(unsigned id){
 SnapFrame *s=snap_recording;if(!s)return 0;
 for(unsigned i=0;i<s->texture_count;i++)if(s->textures[i].id==id)return 0;
 if(s->texture_count==s->texture_capacity){
  unsigned capacity=s->texture_capacity?s->texture_capacity*2:32;SnapTexture *p=realloc(s->textures,capacity*sizeof(*p));if(!p){snap_error=1;return 0;}s->textures=p;s->texture_capacity=capacity;
 }
 s->textures[s->texture_count++]=(SnapTexture){id,host_textures[id]};return 1;
}
static int snap_commit(void){
 SnapFrame *s=snap_recording;if(!s)return 0;
 for(unsigned i=0;i<snap_region_count;i++){
  SnapRegion *r=snap_regions+i;ULONG_PTR count=65552;DWORD granularity=0;
  if(GetWriteWatch(WRITE_WATCH_FLAG_RESET,r->base,r->size,snap_addresses,&count,&granularity)||granularity!=4096||count>65552){snap_error=1;break;}
  if(s->count+count>SNAP_MAX_PAGES){snap_error=1;break;}
  size_t required=s->count+count;
  if(required>s->capacity){size_t capacity=s->capacity?s->capacity:64;while(capacity<required)capacity*=2;if(capacity>SNAP_MAX_PAGES)capacity=SNAP_MAX_PAGES;
   SnapPage *p=realloc(s->pages,capacity*sizeof(*p));if(!p){snap_error=1;break;}s->pages=p;s->capacity=capacity;
  }
  for(ULONG_PTR j=0;j<count;j++){
   size_t offset=(unsigned char *)snap_addresses[j]-r->base;if(offset+4096>r->size){snap_error=1;break;}
   SnapPage *p=s->pages+s->count++;p->region=i;p->offset=(uint32_t)offset;
   memcpy(p->bytes,r->shadow+offset,4096);memcpy(r->shadow+offset,r->base+offset,4096);
  }
 }
 snap_captured_pages+=s->count;if(s->count>snap_peak_pages)snap_peak_pages=s->count;
 snap_recording=NULL;return !snap_error;
}
static int snap_restore(uint32_t first,uint32_t tip){
 if(first>=tip||tip-first>ROLLBACK_MAX||snap_recording||snap_error)return 0;
 for(uint32_t f=first;f<tip;f++){SnapFrame *s=&snap_frames[f%SNAP_SLOTS];if(!s->valid||s->frame!=f)return 0;}
 for(uint32_t f=tip;f>first;){SnapFrame *s=&snap_frames[(--f)%SNAP_SLOTS];
  for(size_t j=0;j<s->count;j++){SnapPage *p=s->pages+j;SnapRegion *r=snap_regions+p->region;memcpy(r->base+p->offset,p->bytes,4096);memcpy(r->shadow+p->offset,p->bytes,4096);}
  for(unsigned j=0;j<s->texture_count;j++){SnapTexture *t=s->textures+j;free(host_textures[t->id].pixels);host_textures[t->id]=t->previous;t->previous.pixels=NULL;}
  captured_textures=s->textures_before;captured_palettes=s->palettes_before;versus_result_seen=s->result_seen;versus_confirm_armed=s->confirm_armed;
  snap_discard(s);
 }
 for(unsigned i=0;i<snap_region_count;i++)if(ResetWriteWatch(snap_regions[i].base,snap_regions[i].size))return 0;
 snap_rewinds++;return 1;
}
static void snap_close(void){
 snap_recording=NULL;for(unsigned i=0;i<SNAP_SLOTS;i++){snap_discard(&snap_frames[i]);free(snap_frames[i].pages);free(snap_frames[i].textures);memset(&snap_frames[i],0,sizeof(SnapFrame));}
 for(unsigned i=0;i<snap_region_count;i++){if(snap_regions[i].shadow)VirtualFree(snap_regions[i].shadow,0,MEM_RELEASE);snap_regions[i].shadow=NULL;}
 free(snap_addresses);snap_addresses=NULL;snap_initialized=0;
}
#endif
