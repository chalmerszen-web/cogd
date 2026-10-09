#ifndef TEST_ADC_CLOCK_HAL_H
#define TEST_ADC_CLOCK_HAL_H
#include <stdint.h>
typedef struct { unsigned unused; } adc_hal_dma_ctx_t;
enum { ADC_DIGI_CLK_SRC_DEFAULT=1,TEST_ADC_OTHER_SOURCE=2 };
typedef struct { uint32_t sample_freq_hz,clk_src,clk_src_freq_hz; } adc_hal_digi_ctrlr_cfg_t;
#endif
