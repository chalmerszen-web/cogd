/* Replay archived measured levels/spectral decisions with exact Q15 scores. */
#include "confirmation.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
    if(argc!=6) return 2;
    char *end; unsigned long noise=strtoul(argv[3],&end,10);
    if(!argv[3][0] || *end || noise>32768) return 2;
    unsigned long lead=strtoul(argv[4],&end,10);
    if(!argv[4][0] || *end || lead>1) return 2;
    FILE *scores=fopen(argv[1],"rb"),*frames=fopen(argv[2],"r"),*out=fopen(argv[5],"w");
    if(!scores || !frames || !out) {
        if(scores) fclose(scores);
        if(frames) fclose(frames);
        if(out) fclose(out);
        return 3;
    }
    agent_confirmation_t c;
    if(agent_confirmation_init(&c,1000,4000,10000)) return 4;
    unsigned ms,level,spectral,count=0;int error=0,scanned=EOF;
    while((scanned=fscanf(frames,"%u %u %u",&ms,&level,&spectral))==3) {
        if(++count>500 || ms!=count*20 || level>32768 || spectral>1) { error=5;break; }
        unsigned wanted=ms/16+(unsigned)lead;
        while(!c.confirmed && c.endpoint.state<AGENT_EP_DONE && c.frames<wanted) {
            unsigned char bytes[2];size_t n=fread(bytes,1,2,scores);
            if(!n && c.frames>=ms/16) break; /* No optional future score at EOF. */
            if(n!=2 || agent_confirmation_score(&c,(unsigned)bytes[0]|(unsigned)bytes[1]<<8)) { error=6;break; }
        }
        unsigned threshold=c.endpoint.state==AGENT_EP_SPEECH?(unsigned)noise*3/2:(unsigned)noise*2;
        if(threshold<240) threshold=240;
        bool accepted=spectral && level>threshold;
        if(error || agent_confirmation_feed(&c,accepted)) { if(!error) error=7;break; }
        fprintf(out,"{\"source_ms\":%u,\"elapsed_ms\":%u,\"state\":%u,\"speech_ms\":%u,\"quiet_ms\":%u,\"accepted\":%s,\"confirmed\":%s,\"confirmed_ms\":%u,\"neural_frames\":%u}\n",
            ms,c.endpoint.elapsed_ms,(unsigned)c.endpoint.state,c.endpoint.speech_ms,c.endpoint.quiet_ms,
            accepted?"true":"false",c.confirmed?"true":"false",c.confirmed_ms,c.frames);
    }
    if(!error && scanned!=EOF) error=5;
    if(ferror(scores) || ferror(frames) || ferror(out)) error=8;
    if(fclose(scores)) error=9;
    if(fclose(frames)) error=9;
    if(fclose(out)) error=9;
    return error;
}
