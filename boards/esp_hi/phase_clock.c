#include "phase.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
unsigned phase_clock(void)
{
    /* The pinned normal configuration is fixed160MHz. No frequency scaling,
     * interrupt masking, forced yields or changes to capture scheduling. */
    return esp_cpu_get_cycle_count();
}
unsigned phase_hz(void) {return (unsigned)esp_clk_cpu_freq();}
