/* Deterministic replay of original board metadata, not a replacement VAD. */
#include "source_stream.h"
#include "asr_end.h"
#include "intent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned ms;char text[2048]; } hint_t;
static hint_t hints[256];
typedef struct { unsigned ms,notice; } notice_t;
static notice_t notices[500];
static unsigned little32(const uint8_t *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}

int main(int argc,char **argv)
{
    if(argc<4)return 2;
    bool supported=false,shadow=false;unsigned transcribed=0,cloud_end=0;
    const char *output=NULL,*hint_path=NULL,*notice_path=NULL;
    for(int i=4;i<argc;++i) {
        if(!strcmp(argv[i],"--supported") && !supported)supported=true;
        else if(!strcmp(argv[i],"--shadow") && !shadow)shadow=true;
        else if(!strcmp(argv[i],"--output") && !output && i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--hints") && !hint_path && i+1<argc)hint_path=argv[++i];
        else if(!strcmp(argv[i],"--notices") && !notice_path && i+1<argc)notice_path=argv[++i];
        else if(!strcmp(argv[i],"--transcribed") && !transcribed && i+1<argc) {
            char *end;unsigned long value=strtoul(argv[++i],&end,10);
            if(!argv[i][0] || *end || !value || value>=4000 || value%20)return 2;
            transcribed=(unsigned)value;
        } else if(!strcmp(argv[i],"--cloud-end") && !cloud_end && i+1<argc) {
            char *end;unsigned long value=strtoul(argv[++i],&end,10);
            if(!argv[i][0] || *end || !value || value>AGENT_ASR_END_MAX_MS)return 2;
            cloud_end=(unsigned)value;
        } else return 2;
    }
    char *end;unsigned long noise=strtoul(argv[2],&end,10);
    if(!argv[2][0] || *end || noise>32768)return 2;
    unsigned long silence=strtoul(argv[3],&end,10);
    if(!argv[3][0] || *end || silence<400 || silence>2000)return 2;
    if(notice_path && (!supported || hint_path || transcribed || cloud_end))return 2;
    if(shadow && !notice_path)return 2;
    /* Avoid WSL's lossy Windows stdout bridge; exclusive files retain evidence. */
    if(output && !freopen(output,"wx",stdout))return 7;
    unsigned hint_count=0,hint_at=0;
    if(hint_path) {
        FILE *in=fopen(hint_path,"r");if(!in)return 3;
        while(hint_count<256 && fscanf(in,"%u\t%2047[^\n]\n",&hints[hint_count].ms,hints[hint_count].text)==2) {
            if(hint_count && hints[hint_count].ms<hints[hint_count-1].ms) {fclose(in);return 3;}
            ++hint_count;
        }
        bool bad=!feof(in) || ferror(in);
        if(fclose(in) || bad)return 3;
    }
    uint8_t records[SOURCE_METADATA_BYTES];
    FILE *in=fopen(argv[1],"rb");if(!in)return 3;
    size_t n=fread(records,1,sizeof(records),in);
    bool bad=ferror(in) || !n || n%SOURCE_RECORD_BYTES || fgetc(in)!=EOF;
    if(fclose(in) || bad)return 3;
    if(shadow && n!=400u*SOURCE_RECORD_BYTES)return 3;
    unsigned notice_count=0,notice_at=0,notice=0;
    if(notice_path) {
        uint8_t data[sizeof(notices)];
        in=fopen(notice_path,"rb");if(!in)return 3;
        size_t size=fread(data,1,sizeof(data),in);
        bad=ferror(in) || !size || size%8 || fgetc(in)!=EOF;
        if(fclose(in) || bad)return 3;
        notice_count=(unsigned)(size/8);
        for(unsigned i=0;i<notice_count;++i) {
            notices[i]=(notice_t){little32(data+i*8),little32(data+i*8+4)};
            if(notices[i].ms%20 || notices[i].ms>=n/6*20 ||
               (!i && notices[i].ms) || (i && (notices[i].ms<=notices[i-1].ms ||
                 notices[i].notice==notices[i-1].notice)))return 3;
        }
    }
    agent_endpoint_t endpoint;
    agent_asr_end_t guard;agent_asr_end_reset(&guard);
    if(agent_endpoint_init(&endpoint,(unsigned)silence,4000,10000))return 4;
    for(unsigned row=0;row<n/SOURCE_RECORD_BYTES;++row) {
        while(hint_at<hint_count && hints[hint_at].ms<=row*20)
            agent_endpoint_hint(&endpoint,agent_speech_pending_argument(hints[hint_at++].text),0);
        source_frame_t frame;
        if(source_stream_read(records,sizeof(records),(unsigned)(n/6)*320,row,&frame))return 5;
        unsigned threshold=(unsigned)noise*2;if(threshold<240)threshold=240;
        bool positive=frame.spectral && frame.level>threshold && frame.clean_level>threshold;
        agent_asr_end_source(&guard,(row+1)*20,positive);
        bool applied=endpoint.state<AGENT_EP_DONE;
        if(notice_at<notice_count && notices[notice_at].ms==row*20) {
            if(!applied && !shadow)return 6;
            notice=notices[notice_at++].notice;
        }
        if(applied) {
            if(notice_path)agent_endpoint_observe(&endpoint,notice);
            if(transcribed && endpoint.elapsed_ms==transcribed && !agent_endpoint_transcribed(&endpoint))return 6;
            if(supported) {
                if(source_stream_endpoint(&endpoint,records,(unsigned)(n/6)*320,(unsigned)noise,&frame))return 6;
            } else agent_endpoint_feed_resume(&endpoint,positive);
        }
        /* Match the capture-probe owner: stop at8s even when speech continues.
         * Keep an earlier real terminal unchanged; normal replays stay10s. */
        if(shadow && row==399 && endpoint.state<AGENT_EP_DONE)endpoint.state=AGENT_EP_LIMIT;
        printf("{\"ms\":%u,\"level\":%u,\"clean\":%u,\"spectral\":%s,\"positive\":%s,\"applied\":%s,\"state\":%u,\"elapsed_ms\":%u,\"speech_ms\":%u,\"quiet_ms\":%u,\"resume_frames\":%u,\"transcribed_ms\":%u,\"held\":%u,\"pending\":%s,\"held_ms\":%u,\"dense_ms\":%u,\"asr_notice\":%u,\"asr_floor_ms\":%u,\"local_onset\":%s}\n",
            (row+1)*20,frame.level,frame.clean_level,frame.spectral?"true":"false",
            positive?"true":"false",applied?"true":"false",(unsigned)endpoint.state,
            endpoint.elapsed_ms,endpoint.speech_ms,endpoint.quiet_ms,endpoint.resume_frames,endpoint.transcribed_ms,
            endpoint.held_frames,endpoint.pending?"true":"false",endpoint.held_frames*20,
            endpoint.dense_at_ms,endpoint.asr_notice,endpoint.asr_floor_ms,endpoint.local_onset?"true":"false");
    }
    if(notice_at!=notice_count)return 6;
    if(cloud_end) {
        /* A single final applied after the retained source. This isolates the
         * source coverage gate, not network order or latency authorization. */
        agent_asr_end_sentence(&guard,1,cloud_end,true,true,true,0);
        printf("{\"kind\":\"source_guard\",\"source_ms\":%u,\"cloud_end_ms\":%u,\"dense_end_ms\":%u,\"resume_ms\":%u,\"candidate\":%u,\"ready\":%s,\"single_final_late_replay\":true}\n",
            guard.source_ms,cloud_end,guard.dense_end_ms,guard.resume_ms,guard.candidate_id,
            agent_asr_end_ready_after(&guard,true,160,700)?"true":"false");
    }
    return fflush(stdout) || ferror(stdout)?7:0;
}
