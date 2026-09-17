#ifndef AGENT_DEVICES_H
#define AGENT_DEVICES_H
#include "control.h"
typedef struct {
    agent_control_backend_t backend;
    agent_control_t *control;
    atomic_flag guard;
    uint32_t held;
    bool clip_job;
#if AGENT_ENABLE_AUDIO
    agent_audio_ops_t audio;
#endif
} agent_devices_t;
void agent_devices_init(agent_devices_t *,const agent_control_backend_t *,agent_control_t *);
void agent_devices_poll(agent_devices_t *);
agent_err_t agent_devices_light_get(void *,uint8_t rgb[3]);
agent_err_t agent_devices_light_set(void *,const uint8_t rgb[3]);
#endif
