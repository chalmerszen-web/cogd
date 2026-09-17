#include "control.h"
#include "json.h"
#include <limits.h>
#include <string.h>

static bool keys(const cJSON *object,const char *allowed)
{
    if(!cJSON_IsObject(object)) return false;
    for(const cJSON *v=object->child;v;v=v->next) {
        size_t n=strlen(v->string); const char *p=allowed; bool found=false;
        if(!n) return false;
        while((p=strstr(p,v->string))) {
            if((p==allowed || p[-1]==' ') && (!p[n] || p[n]==' ')) { found=true; break; }
            ++p;
        }
        if(!found) return false;
    }
    return true;
}
static bool number(const cJSON *o,const char *key,unsigned low,unsigned high,unsigned fallback,uint32_t *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key); uint64_t n;
    if(!v) { *out=fallback; return fallback!=UINT_MAX; }
    if(!agent_json_uint(v,high,&n) || n<low) return false;
    *out=(uint32_t)n; return true;
}
static bool boolean(const cJSON *o,const char *key,bool fallback,uint32_t *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(v && !cJSON_IsBool(v)) return false;
    *out=v?cJSON_IsTrue(v):fallback; return true;
}
static bool timeout(const cJSON *o,agent_step_t *s)
{
    const char *mode=agent_json_string(o,"on_timeout");
    if(cJSON_GetObjectItemCaseSensitive(o,"on_timeout") && !mode) return false;
    if(mode && strcmp(mode,"abort") && strcmp(mode,"continue")) return false;
    s->proceed=mode && !strcmp(mode,"continue"); return true;
}
static agent_err_t step(const cJSON *o,agent_step_t *s)
{
    const char *op=agent_json_string(o,"op"); if(!op) return AGENT_ERR_ARGUMENT;
    bool ok=false;
    if(!strcmp(op,"light")) {
        s->kind=AGENT_STEP_LIGHT;
        ok=keys(o,"op r g b") && number(o,"r",0,255,UINT_MAX,&s->a) &&
            number(o,"g",0,255,UINT_MAX,&s->b) && number(o,"b",0,255,UINT_MAX,&s->c);
    } else if(!strcmp(op,"wait")) {
        s->kind=AGENT_STEP_WAIT; ok=keys(o,"op ms") && number(o,"ms",1,AGENT_PLAN_MS,UINT_MAX,&s->a);
    } else if(!strcmp(op,"play_score")) {
        s->kind=AGENT_STEP_SCORE;
        ok=keys(o,"op score wait") && number(o,"score",0,AGENT_PLAN_SCORES-1,UINT_MAX,&s->a) && boolean(o,"wait",true,&s->b);
    } else if(!strcmp(op,"mic")) {
        s->kind=AGENT_STEP_MIC;
        ok=keys(o,"op enabled") && cJSON_GetObjectItemCaseSensitive(o,"enabled") && boolean(o,"enabled",false,&s->a);
    } else if(!strcmp(op,"capture")) {
        s->kind=AGENT_STEP_CAPTURE; ok=keys(o,"op ms") && number(o,"ms",100,AGENT_CAPTURE_MS,UINT_MAX,&s->a);
    } else if(!strcmp(op,"replay")) {
        s->kind=AGENT_STEP_REPLAY; ok=keys(o,"op wait") && boolean(o,"wait",true,&s->a);
    } else if(!strcmp(op,"await")) {
        s->kind=AGENT_STEP_AWAIT; const char *resource=agent_json_string(o,"resource");
        ok=keys(o,"op resource timeout_ms on_timeout") && resource &&
            (!strcmp(resource,"speaker") || !strcmp(resource,"capture")) &&
            number(o,"timeout_ms",1,AGENT_PLAN_MS,UINT_MAX,&s->b) && timeout(o,s);
        s->a=resource && !strcmp(resource,"capture");
    } else if(!strcmp(op,"wait_level")) {
        s->kind=AGENT_STEP_SOUND;
        ok=keys(o,"op above timeout_ms on_timeout") && number(o,"above",1,32767,UINT_MAX,&s->a) &&
            number(o,"timeout_ms",1,AGENT_PLAN_MS,UINT_MAX,&s->b) && timeout(o,s);
    } else if(!strcmp(op,"gpio_read")) {
        s->kind=AGENT_STEP_GPIO_READ; ok=keys(o,"op pin") && number(o,"pin",0,21,UINT_MAX,&s->a);
    } else if(!strcmp(op,"gpio_write")) {
        s->kind=AGENT_STEP_GPIO_WRITE;
        ok=keys(o,"op pin value") && number(o,"pin",0,21,UINT_MAX,&s->a) && number(o,"value",0,1,UINT_MAX,&s->b);
    } else if(!strcmp(op,"pwm")) {
        s->kind=AGENT_STEP_PWM;
        ok=keys(o,"op pin hz duty") && number(o,"pin",0,21,UINT_MAX,&s->a) &&
            number(o,"hz",10,5000,UINT_MAX,&s->b) && number(o,"duty",0,1000,UINT_MAX,&s->c);
    } else if(!strcmp(op,"wait_gpio")) {
        s->kind=AGENT_STEP_GPIO_WAIT;
        ok=keys(o,"op pin value timeout_ms on_timeout") && number(o,"pin",0,21,UINT_MAX,&s->a) &&
            number(o,"value",0,1,UINT_MAX,&s->b) && number(o,"timeout_ms",1,AGENT_PLAN_MS,UINT_MAX,&s->c) && timeout(o,s);
    }
    return ok?AGENT_OK:AGENT_ERR_ARGUMENT;
}

agent_err_t agent_plan_parse(const char *text,agent_plan_t *plan)
{
    if(!text || !plan || strlen(text)>AGENT_ARGS_MAX) return AGENT_ERR_LIMIT;
    cJSON *root=agent_json_parse(text,strlen(text)); if(!root) return AGENT_ERR_JSON;
    agent_err_t error=AGENT_ERR_ARGUMENT; agent_plan_t p={0};
    const cJSON *steps=cJSON_GetObjectItemCaseSensitive(root,"steps"),*scores=cJSON_GetObjectItemCaseSensitive(root,"scores");
    if(!keys(root,"steps scores repeat timeout_ms") || !cJSON_IsArray(steps) || !steps->child ||
       (unsigned)cJSON_GetArraySize(steps)>AGENT_PLAN_STEPS || (scores && (!cJSON_IsArray(scores) || (unsigned)cJSON_GetArraySize(scores)>AGENT_PLAN_SCORES))) goto done;
    uint32_t repeat,ms;
    if(!number(root,"repeat",1,8,1,&repeat) || !number(root,"timeout_ms",1,AGENT_PLAN_MS,AGENT_PLAN_MS,&ms)) goto done;
    p.repeat=repeat; p.timeout_ms=ms;
    for(const cJSON *s=steps->child;s;s=s->next) {
        error=step(s,&p.steps[p.count++]); if(error) goto done;
    }
    for(const cJSON *s=scores?scores->child:NULL;s;s=s->next) {
#if AGENT_ENABLE_AUDIO
        error=agent_score_parse_node(s,&p.scores[p.score_count++]); if(error) goto done;
#else
        error=AGENT_ERR_CONFIG; goto done;
#endif
    }
    *plan=p; error=AGENT_OK;
done:
    cJSON_Delete(root); return error;
}

static bool pin_allowed(const agent_control_backend_t *b,unsigned pin,unsigned mode)
{
    for(size_t i=0;i<b->pin_count;++i)
        if(b->pins[i].pin==pin) return (b->pins[i].modes&mode)!=0;
    return false;
}
agent_err_t agent_plan_validate(const agent_plan_t *p,const agent_control_backend_t *b,uint32_t *resources,unsigned *worst_ms)
{
    if(!p || !b || !b->resources || (!b->pins && b->pin_count) || !resources || !worst_ms || !p->count || p->count>AGENT_PLAN_STEPS ||
       !p->repeat || p->repeat>8 || !p->timeout_ms || p->timeout_ms>AGENT_PLAN_MS || p->score_count>AGENT_PLAN_SCORES)
        return AGENT_ERR_ARGUMENT;
    uint32_t mask=0,pwm_pins=0; uint64_t duration=0;
#if AGENT_ENABLE_AUDIO
    agent_audio_state_t audio={0};
    if(b->audio && b->audio->inspect) {
        agent_err_t error=b->audio->inspect(b->audio->ctx,&audio); if(error) return error;
    }
    bool mic=audio.mic_requested,clip=audio.clip_ready,pending=false;
    unsigned clip_ms=audio.clip_ms;
    for(unsigned i=0;i<p->score_count;++i) { agent_err_t error=agent_score_validate(&p->scores[i]); if(error) return error; }
#endif
    /* State carries across repeats: a prior iteration may disable the mic or replace the clip. */
    for(unsigned i=0;i<p->count*p->repeat;++i) {
        const agent_step_t *s=&p->steps[i%p->count];
        if(s->kind==AGENT_STEP_LIGHT) {
            if(s->a>255 || s->b>255 || s->c>255) return AGENT_ERR_ARGUMENT;
            if(!b->light_get || !b->light_set || !(b->light_mask&AGENT_RES_LIGHT)) return AGENT_ERR_CONFIG;
            mask|=b->light_mask;
        } else if(s->kind==AGENT_STEP_WAIT) {
            if(!s->a || s->a>AGENT_PLAN_MS) return AGENT_ERR_ARGUMENT;
            duration+=s->a;
        } else if(s->kind>=AGENT_STEP_GPIO_READ && s->kind<=AGENT_STEP_GPIO_WAIT) {
            unsigned mode=s->kind==AGENT_STEP_PWM?AGENT_GPIO_PWM:s->kind==AGENT_STEP_GPIO_WRITE?AGENT_GPIO_WRITE:AGENT_GPIO_READ;
            if(s->a>21 || !pin_allowed(b,s->a,mode)) return AGENT_ERR_FORBIDDEN;
            if(!b->pin_get || ((mode!=AGENT_GPIO_READ) && !b->pin_set)) return AGENT_ERR_CONFIG;
            if(s->kind==AGENT_STEP_GPIO_WRITE && s->b>1) return AGENT_ERR_ARGUMENT;
            if(s->kind==AGENT_STEP_PWM && (s->b<10 || s->b>5000 || s->c>1000)) return AGENT_ERR_ARGUMENT;
            if(s->kind==AGENT_STEP_PWM) pwm_pins|=AGENT_PIN(s->a);
            if(s->kind==AGENT_STEP_GPIO_WAIT) {
                if(s->b>1 || !s->c || s->c>AGENT_PLAN_MS) return AGENT_ERR_ARGUMENT;
                duration+=s->c;
            }
            mask|=AGENT_PIN(s->a);
        } else {
#if AGENT_ENABLE_AUDIO
            const agent_audio_ops_t *a=b->audio;
            if(!a || !a->inspect || !a->stop || !audio.ready) return AGENT_ERR_CONFIG;
            if(s->kind==AGENT_STEP_SCORE) {
                if(s->a>=p->score_count || s->b>1 || pending) return AGENT_ERR_ARGUMENT;
                if(!a->play) return AGENT_ERR_CONFIG;
                if(!(b->speaker_mask&AGENT_RES_SPEAKER)) return AGENT_ERR_CONFIG;
                mask|=b->speaker_mask;
                if(s->b) duration+=(p->scores[s->a].samples*1000u+AGENT_AUDIO_RATE-1)/AGENT_AUDIO_RATE+100;
                else pending=true;
            } else if(s->kind==AGENT_STEP_MIC) {
                if(s->a>1) return AGENT_ERR_ARGUMENT;
                if(!a->microphone) return AGENT_ERR_CONFIG;
                if(!(b->mic_mask&AGENT_RES_MIC)) return AGENT_ERR_CONFIG;
                mic=s->a!=0; mask|=b->mic_mask;
            } else if(s->kind==AGENT_STEP_CAPTURE) {
                if(s->a<100 || s->a>AGENT_CAPTURE_MS || pending) return AGENT_ERR_ARGUMENT;
                if(!a->capture || !audio.clip_storage) return AGENT_ERR_CONFIG;
                if(!(b->speaker_mask&AGENT_RES_SPEAKER) || !(b->mic_mask&AGENT_RES_MIC)) return AGENT_ERR_CONFIG;
                mask|=b->speaker_mask|b->mic_mask|AGENT_RES_CLIP;
                duration+=s->a+10000; clip=true; clip_ms=s->a;
            } else if(s->kind==AGENT_STEP_REPLAY) {
                if(s->a>1 || pending) return AGENT_ERR_ARGUMENT;
                if(!clip) return AGENT_ERR_NOT_FOUND;
                if(!a->replay) return AGENT_ERR_CONFIG;
                if(!(b->speaker_mask&AGENT_RES_SPEAKER)) return AGENT_ERR_CONFIG;
                mask|=b->speaker_mask|AGENT_RES_CLIP;
                if(s->a) duration+=clip_ms+2100; else pending=true;
            } else if(s->kind==AGENT_STEP_AWAIT) {
                if(s->a>1 || !s->b || s->b>AGENT_PLAN_MS) return AGENT_ERR_ARGUMENT;
                if(!(b->speaker_mask&AGENT_RES_SPEAKER) || !(b->mic_mask&AGENT_RES_MIC)) return AGENT_ERR_CONFIG;
                mask|=s->a?(b->speaker_mask|b->mic_mask|AGENT_RES_CLIP):b->speaker_mask;
                duration+=s->b; if(!s->a) pending=false;
            } else if(s->kind==AGENT_STEP_SOUND) {
                if(!mic || !s->a || s->a>32767 || !s->b || s->b>AGENT_PLAN_MS) return AGENT_ERR_ARGUMENT;
                if(!(b->mic_mask&AGENT_RES_MIC)) return AGENT_ERR_CONFIG;
                mask|=b->mic_mask; duration+=s->b;
            } else return AGENT_ERR_ARGUMENT;
#else
            return AGENT_ERR_CONFIG;
#endif
        }
    }
#if AGENT_ENABLE_AUDIO
    if(pending) return AGENT_ERR_ARGUMENT; /* Explicitly join asynchronous sound before cleanup. */
#endif
    unsigned channels=0; while(pwm_pins) { pwm_pins&=pwm_pins-1; ++channels; }
    if(mask&b->reserved_mask) return AGENT_ERR_FORBIDDEN;
    if(channels>2 || duration>p->timeout_ms) return AGENT_ERR_LIMIT;
    *resources=mask; *worst_ms=(unsigned)duration; return AGENT_OK;
}
