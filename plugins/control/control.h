#ifndef AGENT_CONTROL_H
#define AGENT_CONTROL_H
#include "resources.h"
#include "audio.h"

#ifndef AGENT_ENABLE_AUDIO
#define AGENT_ENABLE_AUDIO 0
#endif
#define AGENT_PLAN_STEPS 32u
#define AGENT_PLAN_SCORES 2u
#define AGENT_PLAN_MS 60000u
#define AGENT_CAPTURE_MS 10000u

enum { AGENT_GPIO_READ=1, AGENT_GPIO_WRITE=2, AGENT_GPIO_PWM=4 };
typedef struct { uint8_t pin,modes; const char *name; } agent_pin_t;
typedef struct { unsigned mode,value,hz,duty; } agent_pin_state_t;
typedef enum {
    AGENT_STEP_LIGHT, AGENT_STEP_WAIT, AGENT_STEP_SCORE, AGENT_STEP_MIC,
    AGENT_STEP_CAPTURE, AGENT_STEP_REPLAY, AGENT_STEP_AWAIT, AGENT_STEP_SOUND,
    AGENT_STEP_GPIO_READ, AGENT_STEP_GPIO_WRITE, AGENT_STEP_PWM, AGENT_STEP_GPIO_WAIT
} agent_step_kind_t;
typedef struct { agent_step_kind_t kind; uint32_t a,b,c; bool proceed; } agent_step_t;
typedef struct {
    unsigned count,repeat,timeout_ms,score_count;
    agent_step_t steps[AGENT_PLAN_STEPS];
#if AGENT_ENABLE_AUDIO
    agent_score_t scores[AGENT_PLAN_SCORES];
#endif
} agent_plan_t;

typedef struct {
    agent_resources_t *resources;
    const agent_pin_t *pins;
    size_t pin_count;
    uint32_t reserved_mask,light_mask,speaker_mask,mic_mask;
    agent_err_t (*light_get)(void *,uint8_t rgb[3]);
    agent_err_t (*light_set)(void *,const uint8_t rgb[3]);
    agent_err_t (*pin_get)(void *,unsigned pin,agent_pin_state_t *);
    agent_err_t (*pin_set)(void *,unsigned pin,const agent_pin_state_t *);
#if AGENT_ENABLE_AUDIO
    const agent_audio_ops_t *audio;
#endif
    void *ctx;
} agent_control_backend_t;

typedef enum { AGENT_PLAN_IDLE, AGENT_PLAN_ACCEPTED, AGENT_PLAN_RUNNING, AGENT_PLAN_DONE,
    AGENT_PLAN_CANCELLED, AGENT_PLAN_FAILED } agent_plan_status_t;
typedef struct {
    agent_control_backend_t backend;
    agent_plan_t plan;
    atomic_flag admission;
    atomic_bool active,pending,cancelled;
    atomic_uint status,job,step,cycles,last_pin,last_value,total_steps;
    atomic_int error;
    uint32_t resources,direct_held;
    uint64_t started,wait_until,level_after;
    unsigned pc,iteration,waiting;
    bool prepared,restore_mic,finishing,cleaned;
    agent_err_t finish_error;
    uint8_t previous_rgb[3];
    unsigned previous_volume;
    agent_pin_state_t previous_pins[22];
} agent_control_t;

agent_err_t agent_plan_parse(const char *,agent_plan_t *);
agent_err_t agent_plan_validate(const agent_plan_t *,const agent_control_backend_t *,uint32_t *resources,unsigned *worst_ms);
void agent_control_init(agent_control_t *,const agent_control_backend_t *);
agent_err_t agent_control_submit(agent_control_t *,const agent_plan_t *);
/* next=NULL reads; direct changes persist. Plans temporarily own and restore pins. */
agent_err_t agent_control_gpio(agent_control_t *,unsigned pin,const agent_pin_state_t *next,agent_pin_state_t *out);
void agent_control_cancel(agent_control_t *);
void agent_control_tick(agent_control_t *,uint64_t now_ms);
agent_err_t agent_control_status(agent_control_t *,char *,size_t);
agent_err_t agent_control_capabilities(agent_control_t *,char *,size_t);
#endif
