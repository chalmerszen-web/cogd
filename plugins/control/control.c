#include "control.h"
#include "json.h"
#include <string.h>

enum { WAIT_NONE, WAIT_DELAY, WAIT_SPEAKER, WAIT_CAPTURE, WAIT_SOUND, WAIT_GPIO };

void agent_control_init(agent_control_t *c,const agent_control_backend_t *backend)
{
    memset(c,0,sizeof(*c)); c->backend=*backend;
    atomic_flag_clear(&c->admission);
    atomic_init(&c->active,false); atomic_init(&c->pending,false); atomic_init(&c->cancelled,false);
    atomic_init(&c->status,AGENT_PLAN_IDLE); atomic_init(&c->job,0); atomic_init(&c->step,0);
    atomic_init(&c->cycles,0); atomic_init(&c->last_pin,0); atomic_init(&c->last_value,0);
    atomic_init(&c->total_steps,0); atomic_init(&c->error,AGENT_OK);
}

/* Called with admission held. Busy cleanup retains its lease for the next tick. */
static agent_err_t release_direct(agent_control_t *c)
{
    if(!c->direct_held) return AGENT_OK;
    agent_err_t e=agent_resources_release(c->backend.resources,AGENT_OWNER_DIRECT,c->direct_held);
    if(!e) c->direct_held=0;
    return e;
}

agent_err_t agent_control_gpio(agent_control_t *c,unsigned pin,const agent_pin_state_t *next,agent_pin_state_t *out)
{
    if(!c || !out || pin>21) return AGENT_ERR_ARGUMENT;
    const agent_control_backend_t *b=&c->backend;
    unsigned mode=next?next->mode:AGENT_GPIO_READ,modes=0;
    if(mode!=AGENT_GPIO_READ && mode!=AGENT_GPIO_WRITE && mode!=AGENT_GPIO_PWM) return AGENT_ERR_ARGUMENT;
    for(size_t i=0;i<b->pin_count;++i) if(b->pins[i].pin==pin) modes=b->pins[i].modes;
    if(!(modes&mode) || (AGENT_PIN(pin)&(b->reserved_mask|b->light_mask|b->speaker_mask|b->mic_mask))) return AGENT_ERR_FORBIDDEN;
    if(next && ((mode==AGENT_GPIO_WRITE && next->value>1) ||
        (mode==AGENT_GPIO_PWM && (next->hz<10 || next->hz>5000 || next->duty>1000)))) return AGENT_ERR_ARGUMENT;
    if(!b->resources || !b->pin_get || (next && !b->pin_set)) return AGENT_ERR_CONFIG;
    if(atomic_flag_test_and_set(&c->admission)) return AGENT_ERR_BUSY;
    agent_err_t error=AGENT_ERR_BUSY;
    if(atomic_load(&c->active)) goto done;
    error=release_direct(c); if(error) goto done;
    uint32_t added=0;
    error=agent_resources_claim(b->resources,AGENT_OWNER_DIRECT,AGENT_PIN(pin),&added); if(error) goto done;
    c->direct_held|=added;
    if(next) error=b->pin_set(b->ctx,pin,next);
    if(!error) error=b->pin_get(b->ctx,pin,out);
    agent_err_t cleaned=release_direct(c);
    if(!error && cleaned!=AGENT_ERR_BUSY) error=cleaned;
done:
    atomic_flag_clear(&c->admission); return error;
}

agent_err_t agent_control_submit(agent_control_t *c,const agent_plan_t *plan)
{
    if(!c || !plan) return AGENT_ERR_ARGUMENT;
    if(atomic_flag_test_and_set(&c->admission)) return AGENT_ERR_BUSY;
    agent_err_t error=AGENT_ERR_BUSY; uint32_t mask,added; unsigned duration;
    if(atomic_load(&c->active)) goto done;
    error=release_direct(c); if(error) goto done;
    error=agent_plan_validate(plan,&c->backend,&mask,&duration); if(error) goto done;
    error=agent_resources_claim(c->backend.resources,AGENT_OWNER_PLAN,mask,&added); if(error) goto done;
    atomic_store(&c->pending,false); atomic_store(&c->cancelled,false);
    atomic_store(&c->active,true);
    c->plan=*plan; c->resources=mask; c->prepared=false; c->finishing=false; c->cleaned=false;
    c->pc=0; c->iteration=0; c->waiting=WAIT_NONE;
    atomic_store(&c->error,AGENT_OK); atomic_store(&c->step,0); atomic_store(&c->cycles,0);
    atomic_store(&c->total_steps,plan->count); atomic_fetch_add(&c->job,1);
    atomic_store(&c->status,AGENT_PLAN_ACCEPTED);
    atomic_store(&c->pending,true); /* Publish the immutable plan last. */
done:
    atomic_flag_clear(&c->admission); return error;
}

void agent_control_cancel(agent_control_t *c)
{
    if(c && atomic_load(&c->active)) atomic_store(&c->cancelled,true);
}

static uint32_t output_pins(const agent_plan_t *p)
{
    uint32_t mask=0;
    for(unsigned i=0;i<p->count;++i)
        if(p->steps[i].kind==AGENT_STEP_GPIO_WRITE || p->steps[i].kind==AGENT_STEP_PWM)
            mask|=AGENT_PIN(p->steps[i].a);
    return mask;
}

static agent_err_t prepare(agent_control_t *c)
{
    agent_control_backend_t *b=&c->backend; agent_err_t error;
    if(c->resources&AGENT_RES_LIGHT) {
        error=b->light_get(b->ctx,c->previous_rgb); if(error) return error;
    }
    uint32_t pins=output_pins(&c->plan);
    for(unsigned pin=0;pin<22;++pin) if(pins&AGENT_PIN(pin)) {
        error=b->pin_get(b->ctx,pin,&c->previous_pins[pin]); if(error) return error;
    }
#if AGENT_ENABLE_AUDIO
    if(c->resources&(AGENT_RES_SPEAKER|AGENT_RES_MIC|AGENT_RES_CLIP)) {
        agent_audio_state_t state;
        error=b->audio->inspect(b->audio->ctx,&state); if(error) return error;
        /* A backend must not start a new operation outside the resource arbiter. */
        if(((c->resources&AGENT_RES_SPEAKER) && state.playing) || state.recording) return AGENT_ERR_BUSY;
        c->restore_mic=state.mic_requested; c->previous_volume=state.volume;
    }
#endif
    c->prepared=true; return AGENT_OK;
}

static agent_err_t restore(agent_control_t *c)
{
    if(!c->prepared) return AGENT_OK;
    agent_control_backend_t *b=&c->backend; agent_err_t error;
#if AGENT_ENABLE_AUDIO
    if(c->resources&(AGENT_RES_SPEAKER|AGENT_RES_MIC|AGENT_RES_CLIP)) {
        const agent_audio_ops_t *a=b->audio; agent_audio_state_t state;
        if(c->resources&(AGENT_RES_SPEAKER|AGENT_RES_CLIP)) {
            error=a->stop(a->ctx); if(error) return error;
            error=a->inspect(a->ctx,&state); if(error) return error;
            if(state.playing || state.recording) return AGENT_ERR_BUSY;
        }
        if((c->resources&AGENT_RES_MIC) && a->microphone) {
            error=a->microphone(a->ctx,c->restore_mic); if(error) return error;
            error=a->inspect(a->ctx,&state); if(error) return error;
            if(state.mic_enabled!=c->restore_mic) return AGENT_ERR_BUSY;
        }
        if((c->resources&AGENT_RES_SPEAKER) && a->volume) {
            error=a->volume(a->ctx,c->previous_volume); if(error) return error;
        }
    }
#endif
    if(c->resources&AGENT_RES_LIGHT) {
        error=b->light_set(b->ctx,c->previous_rgb); if(error) return error;
    }
    uint32_t pins=output_pins(&c->plan);
    /* Free temporary PWM channels before restoring persistent PWM elsewhere. */
    for(unsigned pwm=0;pwm<2;++pwm)
        for(unsigned pin=0;pin<22;++pin) if((pins&AGENT_PIN(pin)) &&
            (c->previous_pins[pin].mode==AGENT_GPIO_PWM)==(pwm!=0)) {
            error=b->pin_set(b->ctx,pin,&c->previous_pins[pin]); if(error) return error;
        }
    return AGENT_OK;
}

static void finish(agent_control_t *c,agent_err_t error)
{
    if(!c->finishing) { c->finish_error=error; c->finishing=true; }
    if(!c->cleaned) {
        error=restore(c);
        if(error) {
            /* Keep leases until DMA and outputs are restored, even on driver failure. */
            if(error!=AGENT_ERR_BUSY) { atomic_store(&c->error,error); atomic_store(&c->status,AGENT_PLAN_FAILED); }
            return;
        }
        c->cleaned=true;
    }
    error=agent_resources_release(c->backend.resources,AGENT_OWNER_PLAN,c->resources);
    if(error) { atomic_store(&c->error,error); return; }
    atomic_store(&c->error,c->finish_error);
    atomic_store(&c->status,c->finish_error==AGENT_OK?AGENT_PLAN_DONE:
        c->finish_error==AGENT_ERR_CANCELLED?AGENT_PLAN_CANCELLED:AGENT_PLAN_FAILED);
    atomic_store(&c->pending,false); atomic_store(&c->active,false);
}

static agent_err_t wait_ready(agent_control_t *c,uint64_t now,bool *ready)
{
    const agent_step_t *s=&c->plan.steps[c->pc]; *ready=false;
    if(c->waiting==WAIT_DELAY) { *ready=now>=c->wait_until; return AGENT_OK; }
    if(c->waiting==WAIT_GPIO) {
        agent_pin_state_t state; agent_err_t error=c->backend.pin_get(c->backend.ctx,s->a,&state);
        if(error) return error;
        atomic_store(&c->last_pin,s->a); atomic_store(&c->last_value,state.value);
        *ready=state.value==s->b;
    }
#if AGENT_ENABLE_AUDIO
    else {
        const agent_audio_ops_t *a=c->backend.audio; agent_audio_state_t state;
        agent_err_t error=a->inspect(a->ctx,&state); if(error) return error;
        if(c->waiting==WAIT_SPEAKER) {
            *ready=!state.playing; if(*ready && state.play_error) return state.play_error;
        } else if(c->waiting==WAIT_CAPTURE) {
            *ready=!state.recording;
            if(*ready && (state.capture_error || !state.clip_ready))
                return state.capture_error?state.capture_error:AGENT_ERR_STORAGE;
        } else if(c->waiting==WAIT_SOUND) {
            if(state.mic_error) return state.mic_error;
            *ready=state.mic_valid && state.mic_enabled && state.mic_updated_ms>=c->level_after && state.mic_rms>=s->a;
        }
    }
#endif
    if(*ready || now<c->wait_until) return AGENT_OK;
    if(!s->proceed) return AGENT_ERR_TIMEOUT;
#if AGENT_ENABLE_AUDIO
    if(c->waiting==WAIT_SPEAKER || c->waiting==WAIT_CAPTURE) {
        const agent_audio_ops_t *a=c->backend.audio;
        agent_err_t error=a->stop(a->ctx); if(error) return error;
        agent_audio_state_t state; error=a->inspect(a->ctx,&state); if(error) return error;
        if(state.playing || state.recording) return AGENT_ERR_TIMEOUT;
    }
#endif
    *ready=true; return AGENT_OK;
}

static agent_err_t execute(agent_control_t *c,uint64_t now)
{
    const agent_step_t *s=&c->plan.steps[c->pc]; agent_control_backend_t *b=&c->backend;
    agent_err_t error=AGENT_OK;
    c->wait_until=c->started+c->plan.timeout_ms;
    switch(s->kind) {
    case AGENT_STEP_LIGHT: {
        uint8_t rgb[]={(uint8_t)s->a,(uint8_t)s->b,(uint8_t)s->c}; return b->light_set(b->ctx,rgb);
    }
    case AGENT_STEP_WAIT: c->waiting=WAIT_DELAY; c->wait_until=now+s->a; break;
    case AGENT_STEP_GPIO_READ: {
        agent_pin_state_t state; error=b->pin_get(b->ctx,s->a,&state);
        if(!error) { atomic_store(&c->last_pin,s->a); atomic_store(&c->last_value,state.value); }
        break;
    }
    case AGENT_STEP_GPIO_WRITE: case AGENT_STEP_PWM: {
        agent_pin_state_t state={0}; state.mode=s->kind==AGENT_STEP_PWM?AGENT_GPIO_PWM:AGENT_GPIO_WRITE;
        if(state.mode==AGENT_GPIO_PWM) { state.hz=s->b; state.duty=s->c; } else state.value=s->b;
        return b->pin_set(b->ctx,s->a,&state);
    }
    case AGENT_STEP_GPIO_WAIT: c->waiting=WAIT_GPIO; c->wait_until=now+s->c; break;
#if AGENT_ENABLE_AUDIO
    case AGENT_STEP_SCORE:
        error=b->audio->play(b->audio->ctx,&c->plan.scores[s->a]); if(!error && s->b) c->waiting=WAIT_SPEAKER;
        break;
    case AGENT_STEP_MIC: return b->audio->microphone(b->audio->ctx,s->a!=0);
    case AGENT_STEP_CAPTURE:
        error=b->audio->capture(b->audio->ctx,s->a); if(!error) c->waiting=WAIT_CAPTURE;
        break;
    case AGENT_STEP_REPLAY:
        error=b->audio->replay(b->audio->ctx); if(!error && s->a) c->waiting=WAIT_SPEAKER;
        break;
    case AGENT_STEP_AWAIT:
        c->waiting=s->a?WAIT_CAPTURE:WAIT_SPEAKER; c->wait_until=now+s->b; break;
    case AGENT_STEP_SOUND:
        c->waiting=WAIT_SOUND; c->wait_until=now+s->b; c->level_after=now; break;
#endif
    default: return AGENT_ERR_ARGUMENT;
    }
    return error;
}

void agent_control_tick(agent_control_t *c,uint64_t now)
{
    if(c && !atomic_flag_test_and_set(&c->admission)) {
        release_direct(c); atomic_flag_clear(&c->admission);
    }
    if(!c || !atomic_load(&c->active) || !atomic_load(&c->pending)) return;
    if(c->finishing) { finish(c,c->finish_error); return; }
    if(atomic_load(&c->cancelled)) { finish(c,AGENT_ERR_CANCELLED); return; }
    if(!c->prepared) {
        agent_err_t error=prepare(c); if(error) { finish(c,error); return; }
        c->started=now; atomic_store(&c->status,AGENT_PLAN_RUNNING);
    }
    if(now-c->started>c->plan.timeout_ms) { finish(c,AGENT_ERR_TIMEOUT); return; }
    for(unsigned burst=0;burst<4;++burst) {
        if(c->waiting) {
            bool ready; agent_err_t error=wait_ready(c,now,&ready);
            if(error) { finish(c,error); return; }
            if(!ready) return;
            c->waiting=WAIT_NONE; ++c->pc;
        }
        if(c->pc==c->plan.count) {
            atomic_store(&c->cycles,++c->iteration);
            if(c->iteration==c->plan.repeat) { finish(c,AGENT_OK); return; }
            c->pc=0;
        }
        atomic_store(&c->step,c->pc+1);
        agent_err_t error=execute(c,now);
        if(error) { finish(c,error); return; }
        if(c->waiting) return;
        ++c->pc;
    }
}

agent_err_t agent_control_status(agent_control_t *c,char *out,size_t cap)
{
    if(!c || !out || !cap) return AGENT_ERR_ARGUMENT;
    static const char *const states[]={"idle","accepted","running","done","cancelled","failed"};
    agent_json_writer_t w; agent_json_writer_init(&w,out,cap);
    agent_json_printf(&w,"{\"job\":%u,\"state\":\"%s\",\"active\":%s,\"step\":%u,\"steps\":%u,\"cycles\":%u,\"last_pin\":%u,\"last_value\":%u,\"error\":\"%s\"}",
        atomic_load(&c->job),states[atomic_load(&c->status)],atomic_load(&c->active)?"true":"false",
        atomic_load(&c->step),atomic_load(&c->total_steps),atomic_load(&c->cycles),
        atomic_load(&c->last_pin),atomic_load(&c->last_value),agent_err_name(atomic_load(&c->error)));
    return w.error;
}

agent_err_t agent_control_capabilities(agent_control_t *c,char *out,size_t cap)
{
    if(!c || !out || !cap) return AGENT_ERR_ARGUMENT;
    agent_json_writer_t w; agent_json_writer_init(&w,out,cap);
    agent_json_printf(&w,"{\"max_steps\":%u,\"max_repeat\":8,\"max_ms\":%u,\"light\":%s,\"pins\":[",
        AGENT_PLAN_STEPS,AGENT_PLAN_MS,(c->backend.light_mask&AGENT_RES_LIGHT)?"true":"false");
    for(size_t i=0;i<c->backend.pin_count;++i) {
        const agent_pin_t *p=&c->backend.pins[i]; unsigned owner=0;
        agent_err_t error=agent_resources_owner(c->backend.resources,p->pin,&owner); if(error) return error;
        agent_json_printf(&w,"%s{\"pin\":%u,\"name\":",i?",":"",p->pin); agent_json_quote(&w,p->name);
        agent_json_printf(&w,",\"read\":%s,\"write\":%s,\"pwm\":%s,\"owner\":%u}",
            (p->modes&AGENT_GPIO_READ)?"true":"false",(p->modes&AGENT_GPIO_WRITE)?"true":"false",
            (p->modes&AGENT_GPIO_PWM)?"true":"false",owner);
    }
    agent_json_raw(&w,"],\"audio\":");
#if AGENT_ENABLE_AUDIO
    const agent_audio_ops_t *a=c->backend.audio;
    agent_audio_state_t state={0};
    if(a && a->inspect) { agent_err_t e=a->inspect(a->ctx,&state); if(e) return e; }
    agent_json_printf(&w,"{\"score\":%s,\"mic\":%s,\"capture\":%s,\"replay\":%s}",
        a&&a->play?"true":"false",a&&a->microphone?"true":"false",a&&a->capture&&state.clip_storage?"true":"false",a&&a->replay&&state.clip_storage?"true":"false");
#else
    agent_json_raw(&w,"null");
#endif
    agent_json_raw(&w,"}"); return w.error;
}
