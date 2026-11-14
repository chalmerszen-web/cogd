#pragma once
#include <stdio.h>
#include "esp_timer.h"
#ifdef __cplusplus
extern "C" {
#endif
void esp_agent_tutor_event(const char *json);
#ifdef __cplusplus
}
#endif
/* Fixed stage names and numeric identities only; no credentials, image bytes,
 * prompts or model reasoning. Short enough to survive the journal record cap. */
static inline void p4_tutor_trace(const char *stage,unsigned job,unsigned capture,
                                 unsigned question,unsigned revision,unsigned value,int error) {
    char event[288];
    snprintf(event,sizeof(event),"{\"stage\":\"%s\",\"time_ms\":%llu,\"job\":%u,\"capture\":%u,\"question\":%u,\"revision\":%u,\"value\":%u,\"error\":%d}",
        stage,(unsigned long long)(esp_timer_get_time()/1000),job,capture,question,revision,value,error);
    esp_agent_tutor_event(event);
}
