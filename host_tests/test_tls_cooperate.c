#include "tls_cooperate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static char a,b;
static void *current=&a;
static uint64_t clock_ms;
static unsigned delays;
void *xTaskGetCurrentTaskHandle(void) {return current;}
uint64_t esp_agent_now(void) {return clock_ms;}
void vTaskDelay(unsigned ticks) {assert(ticks==1);++delays;++clock_ms;}

int main(void)
{
    unsigned steps,maximum;
    agent_tls_cooperate_step(0);clock_ms=100;agent_tls_cooperate_step(1);
    assert(!delays && !esp_agent_tls_cooperate_current());
    assert(esp_agent_tls_cooperate_begin() && !esp_agent_tls_cooperate_begin());
    agent_tls_cooperate_step(0);clock_ms+=225;agent_tls_cooperate_step(1);
    esp_agent_tls_cooperate_stats(&steps,&maximum);
    assert(steps==1 && maximum==225 && delays==1);
    current=&b;
    assert(!esp_agent_tls_cooperate_current() && !esp_agent_tls_cooperate_begin());
    agent_tls_cooperate_step(0);clock_ms+=50;agent_tls_cooperate_step(1);
    esp_agent_tls_cooperate_end();assert(delays==1);
    current=&a;assert(esp_agent_tls_cooperate_current());
    clock_ms=UINT32_MAX-5u;agent_tls_cooperate_step(0);
    clock_ms+=10;agent_tls_cooperate_step(1);
    esp_agent_tls_cooperate_stats(&steps,&maximum);
    assert(steps==2 && maximum==225 && delays==2);
    esp_agent_tls_cooperate_end();assert(!esp_agent_tls_cooperate_current());
    agent_tls_cooperate_step(1);assert(delays==2);
    current=&b;assert(esp_agent_tls_cooperate_begin());
    esp_agent_tls_cooperate_stats(&steps,&maximum);assert(!steps && !maximum);
    agent_tls_cooperate_step(0);clock_ms+=1;agent_tls_cooperate_step(1);
    esp_agent_tls_cooperate_end();assert(delays==3);
    puts("TLS scope isolation, no retained arena, step yield, reset and clock wrap OK");
}
