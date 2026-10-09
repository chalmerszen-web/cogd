#ifndef TEST_ADC_CLOCK_REGISTERS_H
#define TEST_ADC_CLOCK_REGISTERS_H
#include <stdint.h>
typedef struct {
    struct { uint32_t clkm_div_num,clkm_div_a,clkm_div_b; } apb_adc_clkm_conf;
    struct { uint32_t timer_target,timer_en; } ctrl2;
} test_adc_registers_t;
extern test_adc_registers_t APB_SARADC;
extern unsigned test_adc_base_calls,test_adc_div_calls;
#endif
