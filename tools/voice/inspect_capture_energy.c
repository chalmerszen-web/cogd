/* Offline energy inspection only. No substitute VAD or speech decisions. */
#include "voice.h"
#include "tonal.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(int argc,char **argv)
{
    bool fast=argc==5 && !strcmp(argv[4],"--fast");
    if(argc!=4 && !fast)return 2;
    FILE *input=fopen(argv[1],"rb"),*pcm=fopen(argv[2],"wb"),*csv=fopen(argv[3],"w");
    if(!input || !pcm || !csv)return 3;
    agent_voice_t voice={0};agent_biquad_t cue={0};tonal_filter_t tonal={0};
    agent_biquad_t highpass={0};
    uint32_t raw_sum=0,filtered_sum=0,clean_sum=0;unsigned samples=0;
    fprintf(csv,"end_ms,raw_mean_abs,filtered_mean_abs,clean_mean_abs\n");
    for(;;) {
        int lo=fgetc(input);if(lo==EOF)break;
        int hi=fgetc(input);if(hi==EOF)return 4;
        int16_t raw=(int16_t)((unsigned)lo|((unsigned)hi<<8));
        int16_t filtered=agent_voice_reject_cue(&cue,agent_voice_filter(&voice,raw));
        int16_t clean=tonal_filter_sample(&tonal,filtered);
        if(fast)clean=tonal_highpass_sample(&highpass,clean);
        raw_sum+=(uint32_t)(raw<0?-(int32_t)raw:raw);
        filtered_sum+=(uint32_t)(filtered<0?-(int32_t)filtered:filtered);
        clean_sum+=(uint32_t)(clean<0?-(int32_t)clean:clean);
        fputc((uint8_t)clean,pcm);fputc((uint8_t)((uint16_t)clean>>8),pcm);
        ++samples;
        if(samples%320==0) {
            fprintf(csv,"%u,%u,%u,%u\n",samples/16,raw_sum/320,filtered_sum/320,clean_sum/320);
            raw_sum=filtered_sum=clean_sum=0;
        }
        if(samples>160000)return 5;
    }
    if(ferror(input) || ferror(pcm) || ferror(csv))return 6;
    fclose(input);fclose(pcm);fclose(csv);
    return 0;
}
