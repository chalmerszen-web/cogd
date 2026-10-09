#ifndef ESP_HI_AUDIO_IRQ_H
#define ESP_HI_AUDIO_IRQ_H
#include <stdatomic.h>
#include "esp_adc/adc_continuous.h"
typedef struct {
    atomic_uint *overruns,*wake_lost;
    atomic_bool *wake_requested;
} esp_hi_audio_irq_t;
bool esp_hi_audio_overflow(adc_continuous_handle_t,const adc_continuous_evt_data_t *,void *);
#endif
