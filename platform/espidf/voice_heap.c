#include "voice_heap.h"
#include "esp_heap_caps.h"
#include "esp_private/cache_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* ESP-IDF6.1 heap_caps_get_free_size and multi_heap_free_size are read-only,
 * lock-free counters. Do not use the largest-block walker, logging, allocation,
 * or a blocking lock inside the hook. Skips and overwritten rows are explicit. */
enum { ROWS=16 };
typedef struct { unsigned free,size,caps,ms,sdk_minimum;char task[16]; } row_t;
static row_t rows[ROWS];
static unsigned count,lowest,lowest_sdk,initial_free,initial_sdk;
static atomic_uint skipped;
static atomic_bool active,session;
static atomic_flag writing=ATOMIC_FLAG_INIT;
typedef struct { unsigned ms,free,phase,sdk_minimum; } point_t;
static point_t points[ROWS];
static unsigned point_count;
static atomic_uint points_skipped;
_Static_assert(sizeof(points)==256,"Diagnostic snapshots stay bounded");

void esp_agent_voice_heap_mark(voice_heap_phase_t phase)
{
    if((unsigned)phase>=VOICE_HEAP_PHASE_COUNT || !atomic_load(&active))return;
    if(xPortInIsrContext() || !spi_flash_cache_enabled() || atomic_flag_test_and_set(&writing)) {
        atomic_fetch_add(&points_skipped,1);return;
    }
    if(atomic_load(&active)) {
        points[point_count%ROWS]=(point_t){(unsigned)(esp_timer_get_time()/1000),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)phase,
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)};
        ++point_count;
    }
    atomic_flag_clear(&writing);
}

/* esp_heap_caps.h already supplies the IRAM section attribute. */
void esp_heap_trace_alloc_hook(void *ptr,size_t size,uint32_t caps)
{
    if(!ptr || !atomic_load(&active))return;
    if(xPortInIsrContext() || !spi_flash_cache_enabled() ||
       atomic_flag_test_and_set(&writing)) {atomic_fetch_add(&skipped,1);return;}
    if(!atomic_load(&active)) {atomic_flag_clear(&writing);return;}
    unsigned free=(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    unsigned sdk=(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    if(free<lowest || sdk<lowest_sdk) {
        row_t *r=&rows[count%ROWS];
        r->free=free;r->sdk_minimum=sdk;r->size=(unsigned)size;r->caps=caps;
        if(free<lowest)lowest=free;
        if(sdk<lowest_sdk)lowest_sdk=sdk;
        r->ms=(unsigned)(esp_timer_get_time()/1000);
        const char *name=pcTaskGetName(NULL);unsigned i=0;
        while(i+1<sizeof(r->task) && name[i]) {r->task[i]=name[i];++i;}
        r->task[i]=0;++count;
    }
    atomic_flag_clear(&writing);
}
void esp_agent_voice_heap_begin(void)
{
    if(atomic_load(&session)) {esp_agent_voice_heap_mark(VOICE_HEAP_CAPTURE_BEGIN);return;}
    atomic_store(&active,false);
    while(atomic_flag_test_and_set(&writing))vTaskDelay(1);
    count=0;lowest=(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    lowest_sdk=(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    initial_free=lowest;initial_sdk=lowest_sdk;
    memset(rows,0,sizeof(rows));atomic_store(&skipped,0);
    point_count=0;memset(points,0,sizeof(points));atomic_store(&points_skipped,0);
    atomic_flag_clear(&writing);
    atomic_store(&active,true);
    esp_agent_voice_heap_mark(VOICE_HEAP_CAPTURE_BEGIN);
}
void esp_agent_voice_heap_end(void (*event)(void *,const char *,const char *))
{
    if(atomic_load(&session))return;
    atomic_store(&active,false);
    /* A lower-priority hook can have been preempted mid-row. This wait is in
     * the ordinary owner, never inside an allocation/free callback. */
    while(atomic_flag_test_and_set(&writing))vTaskDelay(1);
    atomic_flag_clear(&writing);
    if(!event)return;
    char text[256];
    unsigned first=count>ROWS?count-ROWS:0;
    for(unsigned i=first;i<count;++i) {
        const row_t *r=&rows[i%ROWS];
        snprintf(text,sizeof(text),"{\"seq\":%u,\"at_ms\":%u,\"free\":%u,\"sdk_minimum\":%u,\"allocation\":%u,\"caps\":%u,\"task\":\"%s\"}",
            i+1,r->ms,r->free,r->sdk_minimum,r->size,r->caps,r->task);
        event(NULL,"voice_heap",text);
    }
    snprintf(text,sizeof(text),"{\"minimum\":%u,\"sdk_minimum\":%u,\"initial\":%u,\"initial_sdk_minimum\":%u,\"minima\":%u,\"overwritten\":%u,\"skipped\":%u,\"storage\":%u}",
        lowest,lowest_sdk,initial_free,initial_sdk,count,first,atomic_load(&skipped),(unsigned)sizeof(rows));
    event(NULL,"voice_heap_summary",text);
    static const char *const names[]={"capture_begin","answer_end","rearm_begin","rearm_end",
        "http_release_begin","http_release_end","kws_alloc_begin","kws_alloc_end","cleanup_end",
        "workspace_restore_begin","workspace_restore_end","voice_off_begin","voice_off_end"};
    _Static_assert(sizeof(names)/sizeof(*names)==VOICE_HEAP_PHASE_COUNT,"Every phase has a name");
    first=point_count>ROWS?point_count-ROWS:0;
    for(unsigned i=first;i<point_count;++i) {
        const point_t *p=&points[i%ROWS];
        snprintf(text,sizeof(text),"{\"seq\":%u,\"at_ms\":%u,\"free\":%u,\"sdk_minimum\":%u,\"phase\":\"%s\"}",
            i+1,p->ms,p->free,p->sdk_minimum,names[p->phase]);
        event(NULL,"voice_heap_point",text);
    }
    snprintf(text,sizeof(text),"{\"points\":%u,\"overwritten\":%u,\"skipped\":%u,\"storage\":%u}",
        point_count,first,atomic_load(&points_skipped),(unsigned)sizeof(points));
    event(NULL,"voice_heap_points_summary",text);
}
bool esp_agent_voice_heap_session_begin(void)
{
    if(atomic_load(&session) || atomic_load(&active))return false;
    esp_agent_voice_heap_begin();atomic_store(&session,true);return true;
}
bool esp_agent_voice_heap_session_end(void (*event)(void *,const char *,const char *))
{
    if(!atomic_exchange(&session,false))return false;
    esp_agent_voice_heap_end(event);return true;
}
