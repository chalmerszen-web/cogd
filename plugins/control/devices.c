#include "devices.h"
#include "busy_trace.h"
#include <string.h>

static agent_err_t reap(agent_devices_t *d)
{
    uint32_t needed=0;
#if AGENT_ENABLE_AUDIO
    /* Polling empty or light-only leases must not take the audio driver's
     * lock while holding the shared direct-device admission guard. */
    if((d->clip_job || (d->held&(d->backend.speaker_mask|d->backend.mic_mask|AGENT_RES_CLIP))) &&
       d->backend.audio && d->backend.audio->inspect) {
        agent_audio_state_t state;
        agent_err_t e=d->backend.audio->inspect(d->backend.audio->ctx,&state); if(e) return e;
        if(state.playing) needed|=d->backend.speaker_mask|(d->clip_job?AGENT_RES_CLIP:0);
        if(state.recording) needed|=d->backend.speaker_mask|d->backend.mic_mask|AGENT_RES_CLIP;
        if(state.listening) needed|=d->backend.speaker_mask|d->backend.mic_mask|AGENT_RES_CLIP;
        if(state.mic_requested || state.mic_enabled) needed|=d->backend.mic_mask;
        if(!state.playing && !state.recording) d->clip_job=false;
    }
#endif
    uint32_t unused=d->held&~needed;
    if(unused) {
        agent_err_t e=agent_resources_release(d->backend.resources,AGENT_OWNER_DIRECT,unused); if(e) return e;
        d->held&=~unused;
    }
    return AGENT_OK;
}
static agent_err_t claim(agent_devices_t *d,uint32_t mask)
{
    uint32_t added;
    agent_err_t e=agent_resources_claim(d->backend.resources,AGENT_OWNER_DIRECT,mask,&added);
    if(!e) d->held|=added;
    return e;
}
static agent_err_t done(agent_devices_t *d,agent_err_t error)
{
    agent_err_t cleaned=reap(d); atomic_flag_clear(&d->guard);
    if(error) return error;
    cleaned=busy_result(BUSY_DEVICE_CLEANUP,cleaned);
    /* The action was accepted. A busy cleanup keeps its leases until poll;
     * reporting failure here could cause the caller to repeat the effect. */
    return cleaned==AGENT_ERR_BUSY?AGENT_OK:cleaned;
}
void agent_devices_poll(agent_devices_t *d)
{
    if(!d || atomic_flag_test_and_set(&d->guard)) return;
    reap(d); atomic_flag_clear(&d->guard);
}
agent_err_t agent_devices_light_get(void *ctx,uint8_t rgb[3])
{
    agent_devices_t *d=ctx;
    return d && d->backend.light_get?d->backend.light_get(d->backend.ctx,rgb):AGENT_ERR_CONFIG;
}
agent_err_t agent_devices_light_set(void *ctx,const uint8_t rgb[3])
{
    agent_devices_t *d=ctx;
    if(!d || !rgb || !d->backend.light_set) return AGENT_ERR_CONFIG;
    if(atomic_flag_test_and_set(&d->guard)) return busy_result(BUSY_DEVICE_GUARD,AGENT_ERR_BUSY);
    agent_err_t e=claim(d,d->backend.light_mask);
    if(!e) e=d->backend.light_set(d->backend.ctx,rgb);
    return done(d,e);
}
#if AGENT_ENABLE_AUDIO
static agent_err_t inspect(void *ctx,agent_audio_state_t *out)
{
    agent_devices_t *d=ctx; const agent_audio_ops_t *a=d->backend.audio;
    return a && a->inspect?a->inspect(a->ctx,out):AGENT_ERR_CONFIG;
}
static agent_err_t status(void *ctx,char *out,size_t cap)
{
    agent_devices_t *d=ctx; const agent_audio_ops_t *a=d->backend.audio;
    return a && a->status?a->status(a->ctx,out,cap):AGENT_ERR_CONFIG;
}
enum { PLAY,STOP,VOLUME,MIC,CAPTURE,REPLAY,SONG,LISTEN };
static agent_err_t audio_action(agent_devices_t *d,unsigned kind,const agent_score_t *score,const agent_song_t *song,unsigned value)
{
    const agent_audio_ops_t *a=d->backend.audio; if(!a) return AGENT_ERR_CONFIG;
    if(atomic_flag_test_and_set(&d->guard)) return busy_result(BUSY_DEVICE_GUARD,AGENT_ERR_BUSY);
    agent_err_t e=busy_result(BUSY_DEVICE_REAP,reap(d));
    uint32_t mask=kind==MIC?d->backend.mic_mask:d->backend.speaker_mask;
    if(kind==CAPTURE || kind==LISTEN) mask|=d->backend.mic_mask|AGENT_RES_CLIP;
    if(kind==REPLAY) mask|=AGENT_RES_CLIP;
    if(!e) e=busy_result(BUSY_DEVICE_CLAIM,claim(d,mask));
    if(e==AGENT_ERR_BUSY && kind==STOP && d->control) {
        unsigned owner=0;
        if(!agent_resources_owner(d->backend.resources,23,&owner) && owner==AGENT_OWNER_PLAN) {
            agent_control_cancel(d->control); e=AGENT_OK;
            return done(d,e);
        }
    }
    if(!e) {
        agent_audio_state_t state={0};
        e=inspect(d,&state);
        if(!e && state.recording && kind!=STOP && !(kind==LISTEN && !value))
            e=busy_result(BUSY_DEVICE_RECORDING,AGENT_ERR_BUSY);
        if(!e) {
        switch(kind) {
        case PLAY: e=a->play?a->play(a->ctx,score):AGENT_ERR_CONFIG; break;
        case SONG: e=a->play_song?a->play_song(a->ctx,song):AGENT_ERR_CONFIG; break;
        case STOP: e=a->stop?a->stop(a->ctx):AGENT_ERR_CONFIG; break;
        case VOLUME: e=a->volume?a->volume(a->ctx,value):AGENT_ERR_CONFIG; break;
        case MIC: e=a->microphone?a->microphone(a->ctx,value!=0):AGENT_ERR_CONFIG; break;
        case CAPTURE: e=a->capture?a->capture(a->ctx,value):AGENT_ERR_CONFIG; break;
        case REPLAY: e=a->replay?a->replay(a->ctx):AGENT_ERR_CONFIG; break;
        case LISTEN: e=a->listen?a->listen(a->ctx,value!=0):AGENT_ERR_CONFIG; break;
        default: e=AGENT_ERR_ARGUMENT;
        }
        e=busy_result(BUSY_DEVICE_BACKEND,e);
        }
        if(!e && (kind==CAPTURE || kind==REPLAY)) d->clip_job=true;
    }
    return done(d,e);
}
static agent_err_t play(void *ctx,const agent_score_t *score) { return audio_action(ctx,PLAY,score,NULL,0); }
static agent_err_t stop(void *ctx) { return audio_action(ctx,STOP,NULL,NULL,0); }
static agent_err_t volume(void *ctx,unsigned percent) { return audio_action(ctx,VOLUME,NULL,NULL,percent); }
static agent_err_t mic(void *ctx,bool enabled) { return audio_action(ctx,MIC,NULL,NULL,enabled); }
static agent_err_t capture(void *ctx,unsigned ms) { return audio_action(ctx,CAPTURE,NULL,NULL,ms); }
static agent_err_t replay(void *ctx) { return audio_action(ctx,REPLAY,NULL,NULL,0); }
static agent_err_t listen(void *ctx,bool enabled) { return audio_action(ctx,LISTEN,NULL,NULL,enabled); }
static agent_err_t song(void *ctx,const agent_song_t *s) { return audio_action(ctx,SONG,NULL,s,0); }
static agent_err_t read_song(void *ctx,agent_write_fn write,void *out)
{
    agent_devices_t *d=ctx; const agent_audio_ops_t *a=d->backend.audio;
    return a && a->read_song?a->read_song(a->ctx,write,out):AGENT_ERR_CONFIG;
}
#endif
void agent_devices_init(agent_devices_t *d,const agent_control_backend_t *backend,agent_control_t *control)
{
    memset(d,0,sizeof(*d)); d->backend=*backend; d->control=control; atomic_flag_clear(&d->guard);
#if AGENT_ENABLE_AUDIO
    d->audio=(agent_audio_ops_t){.ctx=d,.play=play,.stop=stop,.volume=volume,.microphone=mic,
        .capture=capture,.replay=replay,.status=status,.inspect=inspect,.play_song=song,.read_song=read_song,.listen=listen};
#endif
}
