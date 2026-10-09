#include "adc_clock.h"
#include "hal/adc_hal.h"
#include "soc/apb_saradc_struct.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

test_adc_registers_t APB_SARADC;
unsigned test_adc_base_calls,test_adc_div_calls;
void __wrap_adc_hal_digi_controller_config(adc_hal_dma_ctx_t *,const adc_hal_digi_ctrlr_cfg_t *);

void __real_adc_hal_digi_controller_config(adc_hal_dma_ctx_t *hal,const adc_hal_digi_ctrlr_cfg_t *cfg)
{
    assert(hal && cfg); ++test_adc_base_calls;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_num=15;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_a=0;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_b=1;
    APB_SARADC.ctrl2.timer_target=cfg->sample_freq_hz?5000000u/(2u*cfg->sample_freq_hz):0;
    APB_SARADC.ctrl2.timer_en=0;
}

static void exact(unsigned rate)
{
    adc_hal_dma_ctx_t hal={0};
    adc_hal_digi_ctrlr_cfg_t cfg={rate,ADC_DIGI_CLK_SRC_DEFAULT,80000000u};
    esp_agent_adc_clock_reset(); assert(!esp_agent_adc_clock_applied(rate));
    unsigned calls=test_adc_div_calls;
    __wrap_adc_hal_digi_controller_config(&hal,&cfg);
    assert(test_adc_div_calls==calls+1 && esp_agent_adc_clock_applied(rate));
    /* Independently use the documented rational clock equation. */
    uint64_t a=APB_SARADC.apb_adc_clkm_conf.clkm_div_a;
    uint64_t b=APB_SARADC.apb_adc_clkm_conf.clkm_div_b;
    uint64_t num=APB_SARADC.apb_adc_clkm_conf.clkm_div_num;
    uint64_t interval=APB_SARADC.ctrl2.timer_target;
    assert(b>=1 && b<=63 && a<b && num<=255 && interval>=30 && interval<=4095);
    assert((uint64_t)cfg.clk_src_freq_hz*b==(uint64_t)rate*2*interval*((num+1)*b+a));
    assert(80000000u!=rate*2u*interval*16u); /* Old SDK integer divider differs. */
    char output[256],short_output[4];
    assert(esp_agent_adc_clock_status(output,sizeof(output))==AGENT_OK);
    assert(strstr(output,"\"clock_applied\":true") && strstr(output,"\"active\":false"));
    APB_SARADC.ctrl2.timer_en=1;
    assert(esp_agent_adc_clock_status(output,sizeof(output))==AGENT_OK);
    assert(strstr(output,"\"active\":true"));
    assert(esp_agent_adc_clock_status(short_output,sizeof(short_output))==AGENT_ERR_LIMIT);
    assert(short_output[3]==0 && esp_agent_adc_clock_status(NULL,256)==AGENT_ERR_ARGUMENT);
    assert(esp_agent_adc_clock_status(output,0)==AGENT_ERR_ARGUMENT);
    ++APB_SARADC.ctrl2.timer_target; assert(!esp_agent_adc_clock_applied(rate));
    --APB_SARADC.ctrl2.timer_target;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_b=38; assert(!esp_agent_adc_clock_applied(rate));
    esp_agent_adc_clock_reset(); assert(!esp_agent_adc_clock_applied(rate));
}

int main(void)
{
    exact(16000); exact(32000);
    adc_hal_dma_ctx_t hal={0};
    const adc_hal_digi_ctrlr_cfg_t cases[]={
        {32000,ADC_DIGI_CLK_SRC_DEFAULT,79999999},
        {32000,TEST_ADC_OTHER_SOURCE,80000000},
        {44100,ADC_DIGI_CLK_SRC_DEFAULT,80000000},
        {0,ADC_DIGI_CLK_SRC_DEFAULT,80000000}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        unsigned base=test_adc_base_calls,div=test_adc_div_calls;
        __wrap_adc_hal_digi_controller_config(&hal,&cases[i]);
        assert(test_adc_base_calls==base+1 && test_adc_div_calls==div);
        assert(APB_SARADC.apb_adc_clkm_conf.clkm_div_b==1 && APB_SARADC.apb_adc_clkm_conf.clkm_div_a==0);
        assert(!esp_agent_adc_clock_applied(16000) && !esp_agent_adc_clock_applied(32000));
    }
    assert(!esp_agent_adc_clock_applied(0) && !esp_agent_adc_clock_applied(44100));
    puts("Exact ADC rational clock, lifecycle, unknown configuration and status checks passed");
    return 0;
}
