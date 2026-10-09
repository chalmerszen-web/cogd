#ifndef TEST_ADC_CLOCK_LL_H
#define TEST_ADC_CLOCK_LL_H
#include "soc/apb_saradc_struct.h"
#include <assert.h>
static inline void adc_ll_digi_controller_clk_div(uint32_t num,uint32_t b,uint32_t a)
{
    assert(test_adc_base_calls>test_adc_div_calls);
    assert(!APB_SARADC.ctrl2.timer_en);
    ++test_adc_div_calls;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_num=num;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_b=b;
    APB_SARADC.apb_adc_clkm_conf.clkm_div_a=a;
}
#endif
