#include "adc_clock.h"
#include "hal/adc_hal.h"
#include "hal/adc_ll.h"
#include <stdatomic.h>
#include <stdio.h>

#if !CONFIG_IDF_TARGET_ESP32C3
#error "Exact ADC clock adapter requires the verified C3 HAL"
#endif

static atomic_uint adc_requested_hz;
void __real_adc_hal_digi_controller_config(adc_hal_dma_ctx_t *,const adc_hal_digi_ctrlr_cfg_t *);

void __wrap_adc_hal_digi_controller_config(adc_hal_dma_ctx_t *hal,const adc_hal_digi_ctrlr_cfg_t *cfg)
{
    atomic_store(&adc_requested_hz,0);
    __real_adc_hal_digi_controller_config(hal,cfg);
    if(cfg->clk_src!=ADC_DIGI_CLK_SRC_DEFAULT || cfg->clk_src_freq_hz!=80000000u ||
       (cfg->sample_freq_hz!=16000u && cfg->sample_freq_hz!=32000u)) return;
    /* APB/(16+1/39)/(2*78) =32k; interval156 gives16k.
     * The SDK already sets that interval, before conversions are enabled. */
    adc_ll_digi_controller_clk_div(15,39,1);
    atomic_store(&adc_requested_hz,cfg->sample_freq_hz);
}

void esp_agent_adc_clock_reset(void) { atomic_store(&adc_requested_hz,0); }

bool esp_agent_adc_clock_applied(unsigned rate)
{
    return (rate==16000u || rate==32000u) && atomic_load(&adc_requested_hz)==rate &&
        APB_SARADC.apb_adc_clkm_conf.clkm_div_num==15u &&
        APB_SARADC.apb_adc_clkm_conf.clkm_div_b==39u &&
        APB_SARADC.apb_adc_clkm_conf.clkm_div_a==1u &&
        APB_SARADC.ctrl2.timer_target==(rate==32000u?78u:156u);
}

agent_err_t esp_agent_adc_clock_status(char *output,size_t capacity)
{
    if(!output || !capacity) return AGENT_ERR_ARGUMENT;
    unsigned rate=atomic_load(&adc_requested_hz);
    int n=snprintf(output,capacity,
        "{\"clock_applied\":%s,\"active\":%s,\"source_hz\":%u,\"requested_hz\":%u,"
        "\"div_num\":%u,\"div_a\":%u,\"div_b\":%u,\"interval\":%u}",
        esp_agent_adc_clock_applied(rate)?"true":"false",APB_SARADC.ctrl2.timer_en?"true":"false",
        rate?80000000u:0u,rate,(unsigned)APB_SARADC.apb_adc_clkm_conf.clkm_div_num,
        (unsigned)APB_SARADC.apb_adc_clkm_conf.clkm_div_a,(unsigned)APB_SARADC.apb_adc_clkm_conf.clkm_div_b,
        (unsigned)APB_SARADC.ctrl2.timer_target);
    return n>=0 && (size_t)n<capacity?AGENT_OK:AGENT_ERR_LIMIT;
}
