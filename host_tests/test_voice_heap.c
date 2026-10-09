/* Diagnostic ring failures must stay explicit; no allocation or blocking. */
#include "voice_heap.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned free_bytes=100000,sdk_minimum=100000,tick,reads,waits;
static bool interrupt,cache=true,reenter,forbid;
static char stages[40][40],payloads[40][256];
static unsigned delivered;
size_t heap_caps_get_free_size(uint32_t caps)
{
    assert(caps==MALLOC_CAP_8BIT);++reads;
    if(reenter) {reenter=false;esp_agent_voice_heap_mark(VOICE_HEAP_KWS_ALLOC_BEGIN);}
    return free_bytes;
}
size_t heap_caps_get_minimum_free_size(uint32_t caps)
{ assert(caps==MALLOC_CAP_8BIT);return sdk_minimum; }
int64_t esp_timer_get_time(void) {return (int64_t)++tick*1000;}
int xPortInIsrContext(void) {return interrupt;}
bool spi_flash_cache_enabled(void) {return cache;}
void vTaskDelay(unsigned ticks) {(void)ticks;++waits;assert(!forbid);}
const char *pcTaskGetName(void *task) {(void)task;return "agent_audio_long_name";}
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n) {assert(!forbid);return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s) {assert(!forbid);return __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n) {assert(!forbid);return __real_realloc(p,n);}
static void event(void *ctx,const char *stage,const char *text)
{
    assert(!ctx && delivered<40 && strlen(stage)<sizeof(stages[0]) && strlen(text)<sizeof(payloads[0]));
    strcpy(stages[delivered],stage);strcpy(payloads[delivered++],text);
}
static const char *named(const char *name,unsigned index)
{
    for(unsigned i=0;i<delivered;++i)if(!strcmp(stages[i],name)) {
        if(!index--)return payloads[i];
    }
    return NULL;
}
int main(void)
{
    forbid=true;
    esp_agent_voice_heap_mark(VOICE_HEAP_REARM_BEGIN);
    assert(reads==0);
    esp_agent_voice_heap_begin();
    esp_agent_voice_heap_mark(VOICE_HEAP_REARM_BEGIN);
    free_bytes=75600;esp_heap_trace_alloc_hook((void *)(uintptr_t)1,24320,1);
    esp_agent_voice_heap_mark(VOICE_HEAP_KWS_ALLOC_END);
    esp_agent_voice_heap_mark(VOICE_HEAP_HTTP_RELEASE_BEGIN);
    free_bytes=92400;esp_agent_voice_heap_mark(VOICE_HEAP_HTTP_RELEASE_END);
    esp_agent_voice_heap_end(event);
    assert(strstr(named("voice_heap",0),"\"allocation\":24320"));
    assert(strstr(named("voice_heap",0),"\"task\":\"agent_audio_lon\""));
    assert(strstr(named("voice_heap_summary",0),"\"minimum\":75600"));
    assert(strstr(named("voice_heap_point",1),"rearm_begin"));
    assert(strstr(named("voice_heap_point",2),"\"free\":75600"));
    assert(strstr(named("voice_heap_point",4),"http_release_end"));
    assert(strstr(named("voice_heap_points_summary",0),"\"points\":5,\"overwritten\":0,\"skipped\":0,\"storage\":256"));
    unsigned before=reads;esp_agent_voice_heap_mark(VOICE_HEAP_CLEANUP_END);
    assert(reads==before);

    delivered=0;free_bytes=100000;esp_agent_voice_heap_begin();
    for(unsigned i=0;i<21;++i)esp_agent_voice_heap_mark(VOICE_HEAP_ANSWER_END);
    esp_agent_voice_heap_end(event);
    assert(strstr(named("voice_heap_points_summary",0),"\"points\":22,\"overwritten\":6"));
    assert(strstr(named("voice_heap_point",0),"\"seq\":7,"));
    assert(strstr(named("voice_heap_point",15),"\"seq\":22,"));
    assert(!named("voice_heap_point",16));

    delivered=0;esp_agent_voice_heap_begin();
    interrupt=true;esp_agent_voice_heap_mark(VOICE_HEAP_KWS_ALLOC_BEGIN);
    esp_heap_trace_alloc_hook((void *)(uintptr_t)1,1,1);interrupt=false;
    cache=false;esp_agent_voice_heap_mark(VOICE_HEAP_KWS_ALLOC_BEGIN);cache=true;
    reenter=true;free_bytes=80000;esp_heap_trace_alloc_hook((void *)(uintptr_t)1,128,1);
    esp_agent_voice_heap_mark((voice_heap_phase_t)-1);
    esp_agent_voice_heap_end(event);
    assert(strstr(named("voice_heap_points_summary",0),"\"points\":1,\"overwritten\":0,\"skipped\":3"));
    assert(strstr(named("voice_heap_summary",0),"\"skipped\":1"));

    delivered=0;free_bytes=100000;sdk_minimum=90000;
    assert(esp_agent_voice_heap_session_begin());
    assert(!esp_agent_voice_heap_session_begin());
    for(unsigned i=0;i<3;++i) {
        esp_agent_voice_heap_begin();
        free_bytes=60000-i*1000;sdk_minimum=55000-i*1000;
        esp_heap_trace_alloc_hook((void *)(uintptr_t)1,1024,1);
        esp_agent_voice_heap_end(event);
        assert(delivered==0);
    }
    free_bytes=98000;sdk_minimum=46464;
    esp_agent_voice_heap_mark(VOICE_HEAP_VOICE_OFF_BEGIN);
    esp_heap_trace_alloc_hook((void *)(uintptr_t)1,32768,1);
    esp_agent_voice_heap_mark(VOICE_HEAP_WORKSPACE_RESTORE_END);
    assert(esp_agent_voice_heap_session_end(event));
    assert(!esp_agent_voice_heap_session_end(event));
    assert(strstr(named("voice_heap",3),"\"free\":98000,\"sdk_minimum\":46464"));
    assert(strstr(named("voice_heap_summary",0),"\"minimum\":58000,\"sdk_minimum\":46464"));
    assert(strstr(named("voice_heap_summary",0),"\"minima\":4"));
    assert(strstr(named("voice_heap_points_summary",0),"\"points\":6"));
    unsigned calls=reads;esp_heap_trace_alloc_hook((void *)(uintptr_t)1,1,1);
    esp_agent_voice_heap_mark(VOICE_HEAP_CLEANUP_END);assert(reads==calls);
    assert(waits==0);forbid=false;
    puts("voice heap marker: ownership, bounds, loss reporting and no allocation/wait passed");
}
