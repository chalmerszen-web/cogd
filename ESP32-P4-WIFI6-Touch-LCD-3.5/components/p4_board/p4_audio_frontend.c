#include "p4_audio_frontend.h"
#include "p4_highpass.h"
#include "esp_agc.h"
#include "esp_ns.h"
#include "esp_vad.h"
#include <stdlib.h>
#include <stdatomic.h>

struct p4_audio_frontend { ns_handle_t noise;void *gain;vad_handle_t vad;bool highpass,voiced;unsigned level,input_level;p4_highpass_t filter; };
static atomic_uint settings=3u|(18u<<8);

bool p4_audio_frontend_configure(unsigned profile,unsigned gain_db)
{
    if(profile>7 || gain_db>24)return false;
    atomic_store(&settings,profile|(gain_db<<8));return true;
}

void p4_audio_frontend_close(p4_audio_frontend_t *s)
{
    if(!s)return;
    if(s->noise)ns_destroy(s->noise);
    if(s->gain)esp_agc_close(s->gain);
    if(s->vad)vad_destroy(s->vad);
    free(s);
}
p4_audio_frontend_t *p4_audio_frontend_open(void)
{
    p4_audio_frontend_t *s=calloc(1,sizeof(*s));if(!s)return NULL;
    /* Keep the analogue ADC unchanged. Fixed digital compression avoids an
     * adaptive gain warm-up suppressing the first quiet utterance; the
     * limiter and -3 dBFS target reserve headroom for louder near speech. */
    unsigned config=atomic_load(&settings),profile=config&255;
    s->highpass=(profile&4)!=0;
    s->vad=vad_create_with_param(VAD_MODE_2,16000,20,20,20);
    if(profile&2)s->noise=ns_pro_create(10,0,16000);
    if(profile&1)s->gain=esp_agc_open(AGC_MODE_3,16000);
    if(!s->vad || ((profile&2) && !s->noise) || ((profile&1) && !s->gain)) {
        p4_audio_frontend_close(s);return NULL;
    }
    if(s->gain)set_agc_config(s->gain,config>>8,1,3);
    return s;
}
bool p4_audio_frontend_process(p4_audio_frontend_t *s,int16_t *pcm,size_t samples)
{
    if(!s || !pcm || samples!=320)return false;
    unsigned input_sum=0;
    for(size_t i=0;i<320;i++)input_sum+=(unsigned)abs((int)pcm[i]);
    s->input_level=input_sum/320;
    int16_t highpassed[160],filtered[320];
    for(size_t i=0;i<samples;i+=160) {
        int16_t *source=pcm+i;
        if(s->highpass) {
            for(size_t j=0;j<160;j++)highpassed[j]=p4_highpass_sample(&s->filter,source[j]);
            source=highpassed;
        }
        if(s->noise)ns_process(s->noise,source,filtered+i);
        else for(size_t j=0;j<160;j++)filtered[i+j]=source[j];
        if(s->gain) {
            if(esp_agc_process(s->gain,filtered+i,pcm+i,160,16000)<0)return false;
        } else for(size_t j=0;j<160;j++)pcm[i+j]=filtered[i+j];
    }
    unsigned sum=0;
    for(size_t i=0;i<320;i++)sum+=(unsigned)abs((int)filtered[i]);
    s->level=sum/320;s->voiced=vad_process(s->vad,filtered,16000,20)==VAD_SPEECH;
    return true;
}
bool p4_audio_frontend_voiced(const p4_audio_frontend_t *s){return s && s->voiced;}
unsigned p4_audio_frontend_level(const p4_audio_frontend_t *s){return s?s->level:0;}
unsigned p4_audio_frontend_input_level(const p4_audio_frontend_t *s){return s?s->input_level:0;}
