/* USB-only replay of saved raw16k PCM through the production filter and vendor
 * VAD. No microphone, speaker, Wi-Fi, NVS or writable partition is opened. */
#include "voice.h"
#include "tonal.h"
#include "crc.h"
#include "esp_vad.h"
#ifdef REPLAY_NS
#include "esp_ns.h"
#endif
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FRAME_SAMPLES=320, FRAME_BYTES=640, MAX_FRAMES=500, LINE_BYTES=1344 };
static vad_handle_t vad;
static agent_voice_t voice;
static agent_biquad_t cue,highpass;
static tonal_filter_t tonal;
static unsigned frame,expected,pcm_crc,metadata_crc;
static int16_t pcm[FRAME_SAMPLES];
static unsigned char raw[FRAME_BYTES];
static char line[LINE_BYTES];
static int64_t last_input;
#ifdef REPLAY_NS
static ns_handle_t noise_suppressor;
static vad_handle_t denoised_vad;
static int16_t ns_input[FRAME_SAMPLES],denoised[FRAME_SAMPLES];
static char ns_hex[FRAME_BYTES*2+1];
static unsigned ns_crc;
#endif

static void reply(const char *fmt,...)
{
#ifdef REPLAY_NS
    static char text[1920]; /* USB-only PCM export, outside measured DSP time. */
#else
    char text[384];
#endif
    va_list args;va_start(args,fmt);
    int n=vsnprintf(text,sizeof(text),fmt,args);va_end(args);
    if(n<=0 || (size_t)n>=sizeof(text))abort();
    unsigned at=0;
    while(at<(unsigned)n) {
        int sent=usb_serial_jtag_write_bytes(text+at,(unsigned)n-at,pdMS_TO_TICKS(1000));
        if(sent<=0)return;
        at+=(unsigned)sent;
    }
}
static void close_vad(void)
{
#ifdef REPLAY_NS
    if(noise_suppressor)ns_destroy(noise_suppressor);
    if(denoised_vad)vad_destroy(denoised_vad);
    noise_suppressor=NULL;denoised_vad=NULL;
#endif
    if(vad)vad_destroy(vad);
    vad=NULL;
}
static void error(const char *code)
{ close_vad();reply("@vadprobe {\"error\":\"%s\"}\n",code); }
static unsigned hex_digit(char c)
{
    if(c>='0' && c<='9')return (unsigned)(c-'0');
    if(c>='a' && c<='f')return (unsigned)(c-'a'+10);
    return 16;
}
static bool number(const char **p,unsigned *out,unsigned base)
{
    if(!**p || **p=='+' || **p=='-')return false;
    char *end;unsigned long n=strtoul(*p,&end,base);
    if(end==*p || (unsigned long)(unsigned)n!=n)return false;
    *out=(unsigned)n;*p=end;return true;
}
static void process(const char *text)
{
    if(!strcmp(text,"status")) {
#ifdef REPLAY_NS
        reply("@vadprobe {\"version\":\"0.11.176-ns-replay\",\"vad_mode\":2,\"input_filter\":\"production\",\"ns\":\"ns_create10_pair\",\"vendor_sha256\":\"%s\",\"active\":%s,\"frame\":%u,\"free_heap\":%u}\n",
            VENDOR_SHA,vad?"true":"false",frame,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));return;
#else
        reply("@vadprobe {\"version\":\"0.11.148-vad-replay-clean\",\"vad_mode\":2,\"input_filter\":\"clean\",\"vendor_sha256\":\"%s\",\"active\":%s,\"frame\":%u,\"free_heap\":%u}\n",
            VENDOR_SHA,vad?"true":"false",frame,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));return;
#endif
    }
    if(!strcmp(text,"abort")) {close_vad();reply("@vadprobe {\"aborted\":true}\n");return;}
    if(!strncmp(text,"begin ",6)) {
        const char *p=text+6;unsigned count;
        if(!number(&p,&count,10) || *p || !count || count>MAX_FRAMES) {error("begin");return;}
        close_vad();
#ifdef REPLAY_NS
        unsigned before=heap_caps_get_free_size(MALLOC_CAP_8BIT);
#endif
        vad=vad_create(VAD_MODE_2);
        if(!vad) {error("memory");return;}
#ifdef REPLAY_NS
        unsigned baseline=heap_caps_get_free_size(MALLOC_CAP_8BIT);
        denoised_vad=vad_create(VAD_MODE_2);
        if(!denoised_vad){error("ns_vad_memory");return;}
        unsigned other=heap_caps_get_free_size(MALLOC_CAP_8BIT);
        /* This pinned C3 binary asserts on 20ms despite the public header.
         * Keep VAD at 20ms, but feed NS two consecutive 10ms blocks. */
        noise_suppressor=ns_create(10);
        if(!noise_suppressor){error("ns_memory");return;}
        unsigned after=heap_caps_get_free_size(MALLOC_CAP_8BIT);
        ns_crc=UINT32_MAX;
#endif
        memset(&voice,0,sizeof(voice));memset(&cue,0,sizeof(cue));
        memset(&highpass,0,sizeof(highpass));memset(&tonal,0,sizeof(tonal));
        frame=0;expected=count;pcm_crc=metadata_crc=UINT32_MAX;
#ifdef REPLAY_NS
        reply("@vadprobe {\"begin\":%u,\"baseline_vad_heap\":%u,\"ns_vad_heap\":%u,\"ns_heap\":%u,\"free_heap\":%u}\n",
            expected,before-baseline,baseline-other,other-after,after);return;
#else
        reply("@vadprobe {\"begin\":%u}\n",expected);return;
#endif
    }
    if(!strcmp(text,"end")) {
        if(!vad || frame!=expected) {error("incomplete");return;}
        close_vad();
#ifdef REPLAY_NS
        reply("@vadprobe {\"frames\":%u,\"pcm_crc32\":\"%08x\",\"metadata_crc32\":\"%08x\",\"ns_crc32\":\"%08x\",\"free_heap\":%u,\"stack\":%u}\n",
            frame,~pcm_crc,~metadata_crc,~ns_crc,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)uxTaskGetStackHighWaterMark(NULL));return;
#else
        reply("@vadprobe {\"frames\":%u,\"pcm_crc32\":\"%08x\",\"metadata_crc32\":\"%08x\",\"free_heap\":%u}\n",
            frame,~pcm_crc,~metadata_crc,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));return;
#endif
    }
    if(strncmp(text,"frame ",6) || !vad) {error("command");return;}
    const char *p=text+6;unsigned index,checksum;
    if(!number(&p,&index,10) || *p++!=' ' || index!=frame || frame>=expected) {error("sequence");return;}
    if(strlen(p)!=FRAME_BYTES*2+9 || p[FRAME_BYTES*2]!=' ') {error("length");return;}
    for(unsigned i=0;i<FRAME_BYTES;++i) {
        unsigned a=hex_digit(p[i*2]),b=hex_digit(p[i*2+1]);
        if(a>15 || b>15) {error("hex");return;}
        raw[i]=(unsigned char)((a<<4)|b);
    }
    p+=FRAME_BYTES*2+1;
    if(!number(&p,&checksum,16) || *p || checksum!=agent_crc32(raw,sizeof(raw))) {error("crc");return;}
    pcm_crc=agent_crc32_update(pcm_crc,raw,sizeof(raw));
    int64_t started=esp_timer_get_time();unsigned clean_sum=0,original_sum=0;
    for(unsigned i=0;i<FRAME_SAMPLES;++i) {
        int16_t sample=(int16_t)((unsigned)raw[i*2]|(unsigned)raw[i*2+1]<<8);
        int16_t original=agent_voice_reject_cue(&cue,agent_voice_filter(&voice,sample));
        int32_t clean=tonal_highpass_sample(&highpass,tonal_filter_sample(&tonal,original));
        /* Diagnostic only: test the existing energy-filtered signal as the
         * vendor classifier input. Raw upload CRC and both energy fields stay
         * independently checkable against the production metadata. */
        original_sum+=(unsigned)(original<0?-original:original);
        clean_sum+=(unsigned)(clean<0?-clean:clean);
#ifdef REPLAY_NS
        pcm[i]=original;
#else
        pcm[i]=(int16_t)clean;
#endif
    }
#ifdef REPLAY_NS
    /* Separate stateful streams, unchanged original PCM and baseline VAD.
     * NS runs from frame0; both VADs preserve the original two-frame skip. */
    memcpy(ns_input,pcm,sizeof(pcm));
    int64_t ns_start=esp_timer_get_time();
    ns_process(noise_suppressor,ns_input,denoised);
    ns_process(noise_suppressor,ns_input+160,denoised+160);
    unsigned ns_us=(unsigned)(esp_timer_get_time()-ns_start);
    unsigned ns_sum=0;
    for(unsigned i=0;i<FRAME_SAMPLES;++i) {
        int32_t sample=denoised[i];ns_sum+=(unsigned)(sample<0?-sample:sample);
    }
    bool ns_spectral=frame>=2 && vad_process(denoised_vad,denoised,16000,20)==VAD_SPEECH;
#endif
    /* Retain the production first-two-frame skip and MODE_2. */
    bool spectral=frame>=2 && vad_process(vad,pcm,16000,20)==VAD_SPEECH;
    unsigned original=original_sum/FRAME_SAMPLES,clean=clean_sum/FRAME_SAMPLES;
    unsigned char record[6]={(unsigned char)original,(unsigned char)(original>>8),
        (unsigned char)clean,(unsigned char)(clean>>8),spectral?1u:0u,0xa6};
    metadata_crc=agent_crc32_update(metadata_crc,record,sizeof(record));
    unsigned elapsed=(unsigned)(esp_timer_get_time()-started);
#ifdef REPLAY_NS
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<FRAME_SAMPLES;++i) {
        unsigned value=(uint16_t)denoised[i];raw[i*2]=(unsigned char)value;raw[i*2+1]=(unsigned char)(value>>8);
    }
    ns_crc=agent_crc32_update(ns_crc,raw,sizeof(raw));
    for(unsigned i=0;i<FRAME_BYTES;++i){ns_hex[i*2]=digits[raw[i]>>4];ns_hex[i*2+1]=digits[raw[i]&15];}
    ns_hex[sizeof(ns_hex)-1]=0;
    reply("@vadprobe {\"frame\":%u,\"level\":%u,\"clean\":%u,\"spectral\":%s,\"us\":%u,\"ns_level\":%u,\"ns_spectral\":%s,\"ns_us\":%u,\"ns_pcm\":\"%s\"}\n",
        frame++,original,clean,spectral?"true":"false",elapsed,ns_sum/FRAME_SAMPLES,
        ns_spectral?"true":"false",ns_us,ns_hex);
#else
    reply("@vadprobe {\"frame\":%u,\"level\":%u,\"clean\":%u,\"spectral\":%s,\"us\":%u}\n",
        frame++,original,clean,spectral?"true":"false",elapsed);
#endif
}
void app_main(void)
{
    usb_serial_jtag_driver_config_t config={.rx_buffer_size=2048,.tx_buffer_size=2048};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    unsigned used=0;bool overflow=false;
    for(;;) {
        unsigned char data[128];
        int count=usb_serial_jtag_read_bytes(data,sizeof(data),pdMS_TO_TICKS(25));
        if(count>0)last_input=esp_timer_get_time();
        for(int i=0;i<count;++i) {
            unsigned char c=data[i];
            if(c=='\r')continue;
            if(c=='\n') {
                if(overflow)error("line_limit");
                else {line[used]=0;process(line);}
                used=0;overflow=false;
            } else if(used+1<sizeof(line) && !overflow)line[used++]=(char)c;
            else overflow=true;
        }
        if(vad && esp_timer_get_time()-last_input>10000000)error("idle_timeout");
    }
}
