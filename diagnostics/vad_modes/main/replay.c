/* USB-only replay of saved raw16k PCM through the production filter and vendor
 * VAD. No microphone, speaker, Wi-Fi, NVS or writable partition is opened. */
#include "voice.h"
#include "tonal.h"
#include "crc.h"
#include "esp_vad.h"
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
static vad_handle_t vad,normal_vad;
static agent_voice_t voice;
static agent_biquad_t cue,highpass;
static tonal_filter_t tonal;
static unsigned frame,expected,pcm_crc,metadata_crc,normal_crc;
static int16_t pcm[FRAME_SAMPLES],normal_pcm[FRAME_SAMPLES];
static unsigned char raw[FRAME_BYTES];
static char line[LINE_BYTES];
static int64_t last_input;

static void reply(const char *fmt,...)
{
    char text[384];va_list args;va_start(args,fmt);
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
{ if(vad)vad_destroy(vad);
    if(normal_vad)vad_destroy(normal_vad);
    vad=normal_vad=NULL; }
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
        reply("@vadprobe {\"version\":\"0.11.207-vad-modes\",\"vad_mode\":2,\"compare_mode\":0,\"vendor_sha256\":\"%s\",\"active\":%s,\"frame\":%u,\"free_heap\":%u}\n",
            VENDOR_SHA,vad?"true":"false",frame,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));return;
    }
    if(!strcmp(text,"abort")) {close_vad();reply("@vadprobe {\"aborted\":true}\n");return;}
    if(!strncmp(text,"begin ",6)) {
        const char *p=text+6;unsigned count;
        if(!number(&p,&count,10) || *p || !count || count>MAX_FRAMES) {error("begin");return;}
        close_vad();unsigned before=(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
        vad=vad_create(VAD_MODE_2);
        if(!vad) {error("memory");return;}
        unsigned baseline=(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
        normal_vad=vad_create(VAD_MODE_0);
        if(!normal_vad){error("normal_memory");return;}
        unsigned after=(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
        memset(&voice,0,sizeof(voice));memset(&cue,0,sizeof(cue));
        memset(&highpass,0,sizeof(highpass));memset(&tonal,0,sizeof(tonal));
        frame=0;expected=count;pcm_crc=metadata_crc=normal_crc=UINT32_MAX;
        reply("@vadprobe {\"begin\":%u,\"mode2_heap\":%u,\"mode0_heap\":%u}\n",
            expected,before-baseline,baseline-after);return;
    }
    if(!strcmp(text,"end")) {
        if(!vad || frame!=expected) {error("incomplete");return;}
        close_vad();reply("@vadprobe {\"frames\":%u,\"pcm_crc32\":\"%08x\",\"metadata_crc32\":\"%08x\",\"normal_crc32\":\"%08x\",\"free_heap\":%u}\n",
            frame,~pcm_crc,~metadata_crc,~normal_crc,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));return;
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
        pcm[i]=original;
        int32_t clean=tonal_highpass_sample(&highpass,tonal_filter_sample(&tonal,original));
        clean_sum+=(unsigned)(clean<0?-clean:clean);
    }
    /* Production skips the first two vendor calls. Compute original energy
     * AFTER the vendor call, matching source_stream.c even if it alters PCM. */
    memcpy(normal_pcm,pcm,sizeof(pcm));
    bool spectral=frame>=2 && vad_process(vad,pcm,16000,20)==VAD_SPEECH;
    if(memcmp(pcm,normal_pcm,sizeof(pcm))){error("vendor_mutates_pcm");return;}
    for(unsigned i=0;i<FRAME_SAMPLES;++i) {
        int32_t sample=pcm[i];original_sum+=(unsigned)(sample<0?-sample:sample);
    }
    unsigned original=original_sum/FRAME_SAMPLES,clean=clean_sum/FRAME_SAMPLES;
    unsigned char record[6]={(unsigned char)original,(unsigned char)(original>>8),
        (unsigned char)clean,(unsigned char)(clean>>8),spectral?1u:0u,0xa6};
    metadata_crc=agent_crc32_update(metadata_crc,record,sizeof(record));
    unsigned elapsed=(unsigned)(esp_timer_get_time()-started);
    int64_t normal_started=esp_timer_get_time();
    bool normal=frame>=2 && vad_process(normal_vad,normal_pcm,16000,20)==VAD_SPEECH;
    unsigned normal_us=(unsigned)(esp_timer_get_time()-normal_started);
    if(memcmp(pcm,normal_pcm,sizeof(pcm))){error("normal_mutates_pcm");return;}
    record[4]=normal?1u:0u;normal_crc=agent_crc32_update(normal_crc,record,sizeof(record));
    reply("@vadprobe {\"frame\":%u,\"level\":%u,\"clean\":%u,\"spectral\":%s,\"us\":%u,\"normal\":%s,\"normal_us\":%u}\n",
        frame++,original,clean,spectral?"true":"false",elapsed,normal?"true":"false",normal_us);
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
