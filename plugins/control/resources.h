#ifndef AGENT_RESOURCES_H
#define AGENT_RESOURCES_H
#include "agent.h"

#define AGENT_PIN(pin) (UINT32_C(1) << (pin))
#define AGENT_RES_ADC AGENT_PIN(22)
#define AGENT_RES_I2S AGENT_PIN(23)
#define AGENT_RES_RMT AGENT_PIN(24)
#define AGENT_RES_CLIP AGENT_PIN(25)
#define AGENT_RES_LIGHT AGENT_RES_RMT
#define AGENT_RES_SPEAKER AGENT_RES_I2S
#define AGENT_RES_MIC AGENT_RES_ADC

enum { AGENT_OWNER_SYSTEM=1, AGENT_OWNER_DIRECT=2, AGENT_OWNER_PLAN=3, AGENT_OWNER_STORAGE=4, AGENT_OWNER_DISPLAY=5, AGENT_RESOURCE_COUNT=26 };
typedef struct {
    atomic_flag guard;
    atomic_uint occupied;
    unsigned owners[AGENT_RESOURCE_COUNT];
} agent_resources_t;

void agent_resources_init(agent_resources_t *);
/* All-or-nothing; added contains only newly acquired resources. */
agent_err_t agent_resources_claim(agent_resources_t *,unsigned owner,uint32_t mask,uint32_t *added);
agent_err_t agent_resources_release(agent_resources_t *,unsigned owner,uint32_t mask);
agent_err_t agent_resources_owner(agent_resources_t *,unsigned index,unsigned *owner);
#endif
