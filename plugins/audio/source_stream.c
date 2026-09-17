#include "source_stream.h"
#include <string.h>
agent_err_t source_stream_init(source_stream_t *s,uint8_t *records,size_t bytes,int16_t *filtered,unsigned noise,source_spectral_fn spectral,void *ctx)
{
    if(!s || !records || bytes<SOURCE_METADATA_BYTES || !filtered || !spectral || noise>32768)return AGENT_ERR_ARGUMENT;
    memset(s,0,sizeof(*s));s->records=records;s->filtered=filtered;s->spectral=spectral;s->ctx=ctx;
    memset(records,0,SOURCE_METADATA_BYTES);
    return source_bound_init(&s->bound,noise);
}
bool source_stream_done(const source_stream_t *s)
{ return s && s->bound.target_samples && s->samples>=s->bound.target_samples; }
agent_err_t source_stream_feed(source_stream_t *s,int16_t sample)
{
    if(!s)return AGENT_ERR_ARGUMENT;
    if(!s->records || !s->filtered || !s->spectral)return AGENT_ERR_CONFIG;
    if(source_stream_done(s))return AGENT_ERR_BUSY;
    if(s->samples==SOURCE_FRAME_COUNT*SOURCE_FRAME_SAMPLES)return AGENT_ERR_LIMIT;
    ++s->samples;
    /* Preserve raw padding through the ceiling16ms block, without inventing
     * an additional complete classical frame beyond the proven20ms bound. */
    if(s->bound.target_samples)return AGENT_OK;
    int16_t original=agent_voice_reject_cue(&s->notch,agent_voice_filter(&s->voice,sample));
    s->filtered[s->used++]=original;
    int32_t clean=tonal_filter_sample(&s->tonal,original);
    s->clean_sum+=(uint32_t)(clean<0?-clean:clean);
    if(s->used<SOURCE_FRAME_SAMPLES)return AGENT_OK;
    /* Match original order: classifier first, then mean level. The first two
     * frames do not enter the stateful WebRTC classifier. */
    bool spectral=s->frames>=2 && s->spectral(s->ctx,s->filtered);
    uint32_t sum=0;
    for(unsigned i=0;i<SOURCE_FRAME_SAMPLES;++i) { int32_t value=s->filtered[i];sum+=(uint32_t)(value<0?-value:value); }
    unsigned level=sum/SOURCE_FRAME_SAMPLES;
    unsigned clean_level=s->clean_sum/SOURCE_FRAME_SAMPLES;
    uint8_t data[SOURCE_RECORD_BYTES]={(uint8_t)level,(uint8_t)(level>>8),
        (uint8_t)clean_level,(uint8_t)(clean_level>>8),spectral?1u:0u,0xa6};
    if(s->frames==SOURCE_FRAME_COUNT)return AGENT_ERR_LIMIT;
    memcpy(s->records+s->frames*SOURCE_RECORD_BYTES,data,sizeof(data));++s->frames;s->used=0;s->clean_sum=0;
    return source_bound_feed(&s->bound,level);
}
agent_err_t source_stream_read(const uint8_t *records,size_t bytes,unsigned published,unsigned frame,source_frame_t *out)
{
    if(!records || !out || bytes<SOURCE_METADATA_BYTES)return AGENT_ERR_ARGUMENT;
    if(published>SOURCE_FRAME_COUNT*SOURCE_FRAME_SAMPLES || frame>=SOURCE_FRAME_COUNT)return AGENT_ERR_LIMIT;
    if(frame>=published/SOURCE_FRAME_SAMPLES)return AGENT_ERR_BUSY;
    uint8_t data[SOURCE_RECORD_BYTES];memcpy(data,records+frame*SOURCE_RECORD_BYTES,sizeof(data));
    unsigned level=(unsigned)data[0]|(unsigned)data[1]<<8;
    unsigned clean_level=(unsigned)data[2]|(unsigned)data[3]<<8;
    if(data[5]!=0xa6 || data[4]>1 || level>32768 || clean_level>32768)return AGENT_ERR_CORRUPT;
    *out=(source_frame_t){level,data[4]!=0,clean_level};return AGENT_OK;
}
agent_err_t source_stream_confirm(agent_confirmation_t *c,const uint8_t *records,unsigned published,unsigned noise,source_frame_t *out)
{
    if(!c || !out || noise>32768)return AGENT_ERR_ARGUMENT;
    if(c->endpoint.state>=AGENT_EP_DONE)return AGENT_ERR_BUSY;
    source_frame_t frame;
    agent_err_t error=source_stream_read(records,SOURCE_METADATA_BYTES,published,c->endpoint.elapsed_ms/20,&frame);
    if(error)return error;
    unsigned threshold=c->endpoint.state==AGENT_EP_SPEECH?noise*3/2:noise*2;
    if(threshold<240)threshold=240;
    error=agent_confirmation_feed(c,frame.spectral && frame.level>threshold &&
        (!c->confirmed || frame.clean_level>threshold));
    if(!error)*out=frame;
    return error;
}
