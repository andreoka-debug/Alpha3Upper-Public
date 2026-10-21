/* Native host audio: original request IDs -> extracted resources.
 * Windows ACM decodes the game's MS ADPCM effects; stb_vorbis decodes music. */
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#define STB_VORBIS_NO_STDIO
#include "third-party/stb_vorbis.c"
typedef struct {unsigned bank,request;const char *path;float gain;} HostSampleSpec;
typedef struct {unsigned request;const char *path;unsigned loop_start,loop_end;float gain;} HostMusicSpec;
#include "public_audio_map.h"
typedef struct {short *pcm;unsigned frames,rate,channels;} AudioSample;
typedef struct {AudioSample *sample;double position;float left,right;int active;} AudioVoice;
static AudioSample sample_cache[sizeof(sample_specs)/sizeof(sample_specs[0])];
static AudioSample music_sample;
static AudioVoice audio_voices[32];
static unsigned audio_voice_cursor,audio_failures,audio_unmapped,audio_fx_started,audio_music_started;
static unsigned music_loop_start,music_loop_end;
static double music_position;
static float music_gain=1,music_fade=1;
static int music_active,audio_offline;
static CRITICAL_SECTION audio_lock;
static HWAVEOUT audio_device;
static HANDLE audio_thread;
static volatile LONG audio_running;
static FILE *audio_capture;
static unsigned audio_capture_bytes;
static unsigned char *audio_read(const char *path,size_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0||n>64000000){fclose(f);return NULL;}
    unsigned char *data=malloc((size_t)n);if(!data){fclose(f);return NULL;}
    if(fread(data,1,n,f)!=(size_t)n){free(data);fclose(f);return NULL;}fclose(f);*size=n;return data;
}
static unsigned audio_u32(const void *p){unsigned n;memcpy(&n,p,4);return n;}
static int audio_load_wave(const char *path,AudioSample *out){
    size_t n=0;unsigned char *data=audio_read(path,&n);if(!data||n<12){free(data);return 0;}
    unsigned char *fmt=NULL,*pcm=NULL;unsigned fn=0,pn=0;
    if(memcmp(data,"RIFF",4)||memcmp(data+8,"WAVE",4)){free(data);return 0;}
    for(size_t p=12;p+8<=n;){unsigned len=audio_u32(data+p+4);if(len>n-p-8)break;
        if(!memcmp(data+p,"fmt ",4)){fmt=data+p+8;fn=len;}
        if(!memcmp(data+p,"data",4)){pcm=data+p+8;pn=len;}
        p+=8+len+(len&1);
    }
    if(!fmt||fn<16||fn>256||!pcm){free(data);return 0;}
    unsigned char sfmt[256]={0};memcpy(sfmt,fmt,fn);WAVEFORMATEX *src=(WAVEFORMATEX *)sfmt;
    if(src->nChannels<1||src->nChannels>2||src->nSamplesPerSec<4000||src->nSamplesPerSec>96000){free(data);return 0;}
    WAVEFORMATEX dst={WAVE_FORMAT_PCM,src->nChannels,src->nSamplesPerSec,src->nSamplesPerSec*src->nChannels*2,src->nChannels*2,16,0};
    short *decoded=NULL;DWORD used=0;
    if(src->wFormatTag==WAVE_FORMAT_PCM&&src->wBitsPerSample==16){decoded=malloc(pn);if(decoded){memcpy(decoded,pcm,pn);used=pn;}}
    else{
        HACMSTREAM stream=NULL;MMRESULT err=acmStreamOpen(&stream,NULL,src,&dst,NULL,0,0,ACM_STREAMOPENF_NONREALTIME);
        if(!err){DWORD cap=0;if(!acmStreamSize(stream,pn,&cap,ACM_STREAMSIZEF_SOURCE)&&cap<64000000){
            decoded=calloc(1,cap);ACMSTREAMHEADER h={0};h.cbStruct=sizeof(h);h.pbSrc=pcm;h.cbSrcLength=pn;h.pbDst=(LPBYTE)decoded;h.cbDstLength=cap;
            if(decoded&&!acmStreamPrepareHeader(stream,&h,0)){
                if(!acmStreamConvert(stream,&h,ACM_STREAMCONVERTF_START|ACM_STREAMCONVERTF_END|ACM_STREAMCONVERTF_BLOCKALIGN))used=h.cbDstLengthUsed;
                acmStreamUnprepareHeader(stream,&h,0);
            }
        }acmStreamClose(stream,0);}
    }
    unsigned channels=src->nChannels,rate=src->nSamplesPerSec;free(data);
    if(!decoded||!used){free(decoded);return 0;}
    *out=(AudioSample){decoded,used/(channels*2),rate,channels};return out->frames!=0;
}
static int audio_load_music(const char *path,AudioSample *out){
    size_t n=0;unsigned char *data=audio_read(path,&n);if(!data)return 0;
    int channels=0,rate=0;short *decoded=NULL;int frames=stb_vorbis_decode_memory(data,(int)n,&channels,&rate,&decoded);free(data);
    if(frames<=0||channels<1||channels>2||rate<=0){free(decoded);return 0;}
    *out=(AudioSample){decoded,(unsigned)frames,(unsigned)rate,(unsigned)channels};return 1;
}
static void audio_start_effect(unsigned packed,unsigned pan){
    unsigned bank=(packed>>8)&255,request=(packed>>16)&255;
    unsigned count=sample_spec_count;
    for(unsigned i=0;i<count;i++)if(sample_specs[i].bank==bank&&sample_specs[i].request==request){
        AudioSample *s=sample_cache+i;
        if(!s->pcm&&!audio_load_wave(sample_specs[i].path,s)){audio_failures++;return;}
        float p=(pan<=127?pan:64)/127.f;float gain=sample_specs[i].gain;
        EnterCriticalSection(&audio_lock);
        AudioVoice *voice=audio_voices+(audio_voice_cursor++%32);
        *voice=(AudioVoice){s,0,gain*fminf(1,2*(1-p)),gain*fminf(1,2*p),1};
        audio_fx_started++;LeaveCriticalSection(&audio_lock);return;
    }
    audio_unmapped++;
}
static void audio_start_music(unsigned request){
    const HostMusicSpec *spec=NULL;
    for(unsigned i=0;i<music_spec_count;i++)if(music_specs[i].request==request){spec=music_specs+i;break;}
    if(!spec){audio_unmapped++;return;}
    AudioSample next={0};if(!audio_load_music(spec->path,&next)){audio_failures++;return;}
    EnterCriticalSection(&audio_lock);music_active=0;free(music_sample.pcm);music_sample=next;music_position=0;
    music_loop_start=spec->loop_start;music_loop_end=spec->loop_end;
    if(music_loop_start>=next.frames||music_loop_end>=next.frames||music_loop_end<=music_loop_start){music_loop_start=music_loop_end=0;}
    music_gain=spec->gain;music_fade=1;music_active=1;audio_music_started++;LeaveCriticalSection(&audio_lock);
}
static void audio_process_events(unsigned char *queue){
    unsigned count=*(unsigned *)(queue+0x34ab4);
    for(unsigned i=0;i<count&&i<64;i++){
        unsigned *e=(unsigned *)(queue+0x346b4+i*16);
#ifdef UPPER_TEST_AUDIO_EVENT
        UPPER_TEST_AUDIO_EVENT(e);
#endif
        if(e[0]==8)audio_start_effect(e[1],e[3]);
        else if(e[0]==1)audio_start_music(e[2]);
        else if(e[0]==0){EnterCriticalSection(&audio_lock);music_active=0;LeaveCriticalSection(&audio_lock);}
        else if(e[0]==2){EnterCriticalSection(&audio_lock);music_fade=fminf(127,e[2])/127.f;LeaveCriticalSection(&audio_lock);}
        else if(e[0]==5&&e[1]==0xffffffff){EnterCriticalSection(&audio_lock);for(unsigned j=0;j<32;j++)audio_voices[j].active=0;LeaveCriticalSection(&audio_lock);}
    }
}
static void audio_mix(short *dst,unsigned frames){
    EnterCriticalSection(&audio_lock);
    for(unsigned i=0;i<frames;i++){
        float left=0,right=0;
        if(music_active&&music_sample.pcm){
            unsigned pos=(unsigned)music_position;
            unsigned end=music_loop_end?music_loop_end+1:music_sample.frames;
            if(pos>=end){if(music_loop_end){music_position=music_loop_start+(music_position-end);pos=(unsigned)music_position;}else{music_active=0;pos=0;}}
            if(music_active){float user_gain=InterlockedCompareExchange(&option_music,0,0)/100.f;short *s=music_sample.pcm+pos*music_sample.channels;left=s[0]*music_gain*music_fade*user_gain;right=s[music_sample.channels-1]*music_gain*music_fade*user_gain;music_position+=music_sample.rate/48000.;}
        }
        for(unsigned j=0;j<32;j++){
            AudioVoice *v=audio_voices+j;if(!v->active)continue;AudioSample *s=v->sample;unsigned pos=(unsigned)v->position;
            if(pos>=s->frames){v->active=0;continue;}
            float user_gain=InterlockedCompareExchange(&option_effects,0,0)/100.f;
            short *p=s->pcm+pos*s->channels;left+=p[0]*v->left*user_gain;right+=p[s->channels-1]*v->right*user_gain;v->position+=s->rate/48000.;
        }
        /* Leave headroom when several hit sounds and voices overlap. */
        left*=.65f;right*=.65f;
        if(fabsf(left)>24576.f)left=copysignf(24576.f+8190.f*tanhf((fabsf(left)-24576.f)/8190.f),left);
        if(fabsf(right)>24576.f)right=copysignf(24576.f+8190.f*tanhf((fabsf(right)-24576.f)/8190.f),right);
        dst[i*2]=(short)fmaxf(-32768,fminf(32767,left));dst[i*2+1]=(short)fmaxf(-32768,fminf(32767,right));
    }
    LeaveCriticalSection(&audio_lock);
}
static DWORD WINAPI audio_worker(void *unused){
    (void)unused;WAVEHDR h[4]={0};short pcm[4][512*2];
    for(unsigned i=0;i<4;i++){h[i].lpData=(LPSTR)pcm[i];h[i].dwBufferLength=sizeof(pcm[i]);waveOutPrepareHeader(audio_device,h+i,sizeof(h[i]));h[i].dwFlags|=WHDR_DONE;}
    while(InterlockedCompareExchange(&audio_running,1,1)){
        for(unsigned i=0;i<4;i++)if(h[i].dwFlags&WHDR_DONE){audio_mix(pcm[i],512);waveOutWrite(audio_device,h+i,sizeof(h[i]));}
        Sleep(2);
    }
    waveOutReset(audio_device);for(unsigned i=0;i<4;i++)waveOutUnprepareHeader(audio_device,h+i,sizeof(h[i]));return 0;
}
static void audio_wav_header(FILE *f,unsigned bytes){
    unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,0,0xee,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    unsigned n=bytes+36;memcpy(h+4,&n,4);memcpy(h+40,&bytes,4);fwrite(h,1,44,f);
}
static void audio_init(int offline){
    if(!public_audio_map_load()){MessageBoxW(NULL,L"Importe novamente os recursos de audio.",L"Alpha3Upper Public",MB_OK|MB_ICONERROR);ExitProcess(53);}
    InitializeCriticalSection(&audio_lock);audio_offline=offline;
    if(offline){audio_capture=fopen("data/audio-test.wav","wb");if(audio_capture)audio_wav_header(audio_capture,0);return;}
    WAVEFORMATEX fmt={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};MMRESULT e=waveOutOpen(&audio_device,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL);
    printf("AUDIO_DEVICE result=%u\n",e);fflush(stdout);
    if(!e){InterlockedExchange(&audio_running,1);audio_thread=CreateThread(NULL,0,audio_worker,NULL,0,NULL);}
}
static void audio_output_frame(void){
    if(audio_offline&&audio_capture){short pcm[800*2];audio_mix(pcm,800);fwrite(pcm,1,sizeof(pcm),audio_capture);audio_capture_bytes+=sizeof(pcm);}
}
static void audio_frame(unsigned char *queue){audio_process_events(queue);audio_output_frame();}
static void audio_shutdown(void){
    if(audio_thread){InterlockedExchange(&audio_running,0);WaitForSingleObject(audio_thread,2000);CloseHandle(audio_thread);waveOutClose(audio_device);}
    if(audio_capture){rewind(audio_capture);audio_wav_header(audio_capture,audio_capture_bytes);fclose(audio_capture);}
    printf("AUDIO_RESULT effects=%u music=%u failed=%u unmapped=%u\n",audio_fx_started,audio_music_started,audio_failures,audio_unmapped);fflush(stdout);
}
