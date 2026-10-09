#include "wav.h"
#include <string.h>

enum { RIFF_HEADER, CHUNK_HEADER, FORMAT, SKIP, DATA, PAD, END };
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned u16(const uint8_t *p) { return (unsigned)p[0]|(unsigned)p[1]<<8; }
static void put32(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(i*8)); }
void agent_wav_header(uint8_t out[44],unsigned rate,uint32_t samples)
{
    memset(out,0,44); memcpy(out,"RIFF",4); put32(out+4,36+samples*2);
    memcpy(out+8,"WAVEfmt ",8); put32(out+16,16); out[20]=1; out[22]=1;
    put32(out+24,rate); put32(out+28,rate*2); out[32]=2; out[34]=16;
    memcpy(out+36,"data",4); put32(out+40,samples*2);
}
void agent_wav_init(agent_wav_t *w,const agent_pcm_sink_t *sink)
{ memset(w,0,sizeof(*w)); w->sink=*sink; w->needed=12; }
static agent_err_t flush(agent_wav_t *w)
{
    agent_err_t e=w->block_used?w->sink.write(w->sink.ctx,w->block,w->block_used):AGENT_OK;
    w->block_used=0; return e;
}
static void next(agent_wav_t *w)
{
    w->used=0; w->needed=8;
    w->stage=w->odd?PAD:w->riff_left?CHUNK_HEADER:END;
}
agent_err_t agent_wav_feed(void *ctx,const char *bytes,size_t n)
{
    agent_wav_t *w=ctx;
    while(n) {
        const uint8_t b=(uint8_t)*bytes++; --n;
        if(w->stage!=RIFF_HEADER) {
            if(!w->riff_left) return AGENT_ERR_PROTOCOL;
            --w->riff_left;
        }
        if(w->stage==RIFF_HEADER || w->stage==CHUNK_HEADER) {
            w->header[w->used++]=b;
            if(w->used<w->needed) continue;
            if(w->stage==RIFF_HEADER) {
                uint32_t size=u32(w->header+4);
                /* The provider's measured streaming-WAV sentinel is 0x7fffffbf. */
                w->unbounded_riff=size==0x7fffffbfu || size==0x7fffffffu || size==0xffffffffu;
                if(memcmp(w->header,"RIFF",4) || memcmp(w->header+8,"WAVE",4) || size<36 || (size>18000000 && !w->unbounded_riff))
                    return AGENT_ERR_PROTOCOL;
                w->riff_left=size-4; w->stage=CHUNK_HEADER; w->used=0; w->needed=8; continue;
            }
            w->remaining=u32(w->header+4); w->odd=(w->remaining&1)!=0; w->used=0;
            bool stream_data=w->unbounded_riff && !memcmp(w->header,"data",4) && w->remaining>18000000;
            if(!stream_data && (uint64_t)w->remaining+w->odd>w->riff_left) return AGENT_ERR_PROTOCOL;
            if(!memcmp(w->header,"fmt ",4)) {
                if(w->format || w->data || w->remaining<16 || w->remaining>4096) return AGENT_ERR_PROTOCOL;
                w->stage=FORMAT;
            } else if(!memcmp(w->header,"data",4)) {
                w->unbounded_data=stream_data;
                if(stream_data) w->odd=false;
                if(!w->format || w->data || !w->remaining || (!stream_data && w->remaining%(2*w->channels)) ||
                   (!w->unbounded_data && w->remaining/(2*w->channels)>w->rate*90u)) return AGENT_ERR_PROTOCOL;
                w->data=true; w->stage=DATA;
                agent_err_t e=w->sink.open(w->sink.ctx,w->rate); if(e) return e;
                w->opened=true;
            } else w->stage=SKIP;
            if(!w->remaining) next(w);
        } else if(w->stage==FORMAT) {
            if(w->used<16) w->header[w->used++]=b;
            if(!--w->remaining) {
                w->channels=u16(w->header+2); w->rate=u32(w->header+4);
                if(u16(w->header)!=1 || (w->channels!=1 && w->channels!=2) || u16(w->header+14)!=16 ||
                   (w->rate!=16000 && w->rate!=24000 && w->rate!=48000) ||
                   u16(w->header+12)!=w->channels*2 || u32(w->header+8)!=w->rate*w->channels*2)
                    return AGENT_ERR_PROTOCOL;
                w->format=true; next(w);
            }
        } else if(w->stage==SKIP) {
            if(!--w->remaining) next(w);
        } else if(w->stage==DATA) {
            w->frame[w->frame_used++]=b;
            if(w->frame_used==w->channels*2) {
                int32_t sample=(int16_t)u16(w->frame);
                if(w->channels==2) sample=(sample+(int16_t)u16(w->frame+2))/2;
                w->block[w->block_used++]=(int16_t)sample; ++w->samples; w->frame_used=0;
                if(w->samples>w->rate*90u) return AGENT_ERR_LIMIT;
                if(w->block_used==240) { agent_err_t e=flush(w); if(e) return e; }
            }
            if(!--w->remaining) next(w);
        } else if(w->stage==PAD) { w->odd=false; next(w); }
        else return AGENT_ERR_PROTOCOL;
    }
    return AGENT_OK;
}
agent_err_t agent_wav_finish(agent_wav_t *w)
{
    bool complete=w->stage==END && !w->riff_left;
    if(w->unbounded_data && w->stage==DATA) complete=true; /* HTTP EOF, not a declared duration. */
    if(!w->opened || !w->data || !complete || w->frame_used || !w->samples)
        return AGENT_ERR_PROTOCOL;
    return flush(w);
}
