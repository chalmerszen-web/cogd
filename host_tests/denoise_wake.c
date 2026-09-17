/* Offline diagnostic: exact production spectral subtraction, fixed quiet profile.
 * Input/output are PCM16 mono. Remove only the documented 64-sample latency. */
#include "noise.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    FILE *profile=fopen(argv[1],"rb");
    if(!profile) return 3;
    agent_noise_t state; agent_noise_init(&state);
    int16_t samples[128]; size_t n,profile_size=0;
    while((n=fread(samples,sizeof(*samples),128,profile))>0) {
        profile_size+=n;
        if(n!=128 || profile_size>16000) { fclose(profile); return 4; }
        agent_noise_profile(&state,samples);
    }
    int error=ferror(profile); fclose(profile);
    if(error || profile_size<2048) return 4;
    agent_noise_ready(&state);
    int16_t input[64],output[64]; size_t previous=0,total=0;
    while((n=fread(input,sizeof(*input),64,stdin))>0) {
        total+=n; if(total>16000*90) return 5;
        memset(input+n,0,(64-n)*sizeof(*input));
        agent_noise_process(&state,input,output);
        if(previous && fwrite(output,sizeof(*output),previous,stdout)!=previous) return 6;
        previous=n;
    }
    if(ferror(stdin)) return 7;
    memset(input,0,sizeof(input)); agent_noise_process(&state,input,output);
    if(previous && fwrite(output,sizeof(*output),previous,stdout)!=previous) return 6;
    return 0;
}
