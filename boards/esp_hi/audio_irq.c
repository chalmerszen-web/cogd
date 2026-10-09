#include "audio_irq.h"
#include "esp_attr.h"
/* Separate non-LTO IRAM unit; context/counters remain in internal data RAM. */
bool IRAM_ATTR esp_hi_audio_overflow(adc_continuous_handle_t handle,const adc_continuous_evt_data_t *event,void *ctx)
{
    (void)handle;(void)event;esp_hi_audio_irq_t *irq=ctx;
    atomic_fetch_add(irq->overruns,1);
    if(atomic_load(irq->wake_requested))atomic_fetch_add(irq->wake_lost,1);
    return false;
}
