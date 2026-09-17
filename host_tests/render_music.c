#include "audio.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le(FILE *f,uint32_t n,unsigned bytes)
{ for(unsigned i=0;i<bytes;++i) { fputc((int)(n&255),f); n>>=8; } }
int main(int argc,char **argv)
{
    if(argc<3 || argc>4) { fprintf(stderr,"render_music input.json output.wav [volume]\n"); return 2; }
    unsigned volume=argc==4?(unsigned)strtoul(argv[3],NULL,10):80;
    if(volume>100) return 2;
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    char input[AGENT_ARGS_MAX+2]; size_t bytes=fread(input,1,sizeof(input)-1,f); fclose(f); input[bytes]=0;
    bool song=strstr(input,"patterns")!=NULL;
    agent_song_t music; agent_song_synth_t band;
    agent_score_t score; agent_synth_t synth;
    agent_err_t error=song?agent_song_parse(input,&music):agent_score_parse(input,&score);
    if(!error) error=song?agent_song_synth_init(&band,&music):agent_synth_init(&synth,&score);
    if(error) { fprintf(stderr,"%s\n",agent_err_name(error)); return 1; }
    uint32_t samples=song?band.samples:score.samples;
    f=fopen(argv[2],"wb"); if(!f) return 2;
    fwrite("RIFF",1,4,f); le(f,36+samples*2,4); fwrite("WAVEfmt ",1,8,f); le(f,16,4);
    le(f,1,2); le(f,1,2); le(f,AGENT_AUDIO_RATE,4); le(f,AGENT_AUDIO_RATE*2,4); le(f,2,2); le(f,16,2);
    fwrite("data",1,4,f); le(f,samples*2,4);
    int16_t pcm[240]; size_t n; uint32_t rendered=0;
    while((n=song?agent_song_render(&band,pcm,240,volume):agent_synth_render(&synth,pcm,240,volume))) {
        for(size_t i=0;i<n;++i) le(f,(uint16_t)pcm[i],2);
        rendered+=(uint32_t)n;
    }
    bool failed=ferror(f)!=0; fclose(f);
    printf("{\"samples\":%u,\"rate\":%u,\"limited\":%u,\"song_bytes\":%u,\"voice_bytes\":%u}\n",
           rendered,AGENT_AUDIO_RATE,song?band.soft_limited:0,(unsigned)sizeof(music),(unsigned)sizeof(band));
    return failed || rendered!=samples;
}
