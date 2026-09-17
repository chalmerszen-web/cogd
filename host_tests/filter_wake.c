/* Offline probe preparation uses the exact production C voice filter. */
#include "voice.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    unsigned gain=(unsigned)atoi(argv[1]),filtered=(unsigned)atoi(argv[2]);
    if(gain<1 || gain>8 || filtered>2) return 2;
    agent_voice_t state; agent_voice_init(&state);
    agent_biquad_t cue={0};
    int16_t samples[512]; size_t n,total=0;
    while((n=fread(samples,sizeof(*samples),512,stdin))>0) {
        total+=n; if(total>16000*90) return 3;
        for(size_t i=0;i<n;++i) {
            int32_t value=filtered?agent_voice_filter(&state,samples[i]):samples[i];
            if(filtered==2) value=agent_voice_reject_cue(&cue,(int16_t)value);
            value*=(int32_t)gain;
            samples[i]=(int16_t)(value>32767?32767:value< -32768?-32768:value);
        }
        if(fwrite(samples,sizeof(*samples),n,stdout)!=n) return 4;
    }
    return ferror(stdin)?5:0;
}
