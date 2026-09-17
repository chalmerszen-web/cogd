#include "vad_backend.h"
#include "ten_memory.h"
#include "ten_vad.h"
#include "model_hook.h"
#include "profile.h"
#include <string.h>

static ten_vad_handle_t instance;
int ten_model_reset(AUP_MODULE_AIVAD *s) { memset(s,0,sizeof(*s)); return 0; }
int ten_model_process(AUP_MODULE_AIVAD *s,const float input[123],float *out)
{
    bool ok=ten_nn_step(&s->nn,input,false,out);
    ++s->frames; return ok?0:-1;
}
/* Per-block wall timing belongs to the adapter, outside this portable kernel. */
void ten_profile_begin(unsigned slot) { (void)slot; }
void ten_profile_end(unsigned slot) { (void)slot; }

agent_err_t esp_hi_vad_open(void)
{ return esp_hi_vad_open_at(NULL,0); }
agent_err_t esp_hi_vad_open_at(void *borrowed,size_t capacity)
{
    if(!borrowed && capacity) return AGENT_ERR_ARGUMENT;
    if(instance || !agent_ten_memory_begin_at(borrowed,capacity)) return AGENT_ERR_BUSY;
    int result=ten_vad_create(&instance,ESP_HI_VAD_SAMPLES,.4f);
    bool complete=agent_ten_memory_seal();
    if(result || !complete) {
        esp_hi_vad_close(); return complete?AGENT_ERR_TOOL:AGENT_ERR_MEMORY;
    }
    return AGENT_OK;
}
agent_err_t esp_hi_vad_process(const int16_t samples[ESP_HI_VAD_SAMPLES],unsigned *probability)
{
    if(!samples || !probability) return AGENT_ERR_ARGUMENT;
    if(!instance) return AGENT_ERR_CONFIG;
    float p; int flag;
    if(ten_vad_process(instance,samples,ESP_HI_VAD_SAMPLES,&p,&flag) ||
       agent_ten_memory_failed() || !(p>=0 && p<=1)) return AGENT_ERR_TOOL;
    *probability=(unsigned)(p*32768); return AGENT_OK;
}
void esp_hi_vad_close(void)
{
    /* TEN owns only memory; release the allocation group on success and on
     * every partially constructed failure, without unsafe partial destructors. */
    instance=NULL; agent_ten_memory_close();
}
size_t esp_hi_vad_bytes(void) { return agent_ten_memory_bytes(); }
size_t esp_hi_vad_heap_bytes(void) { return agent_ten_memory_heap_bytes(); }
size_t esp_hi_vad_arena_bytes(void) { return agent_ten_memory_arena_bytes(); }
