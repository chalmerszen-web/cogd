#ifndef AGENT_DECIMATE_H
#define AGENT_DECIMATE_H
#include <stdbool.h>
#include <stdint.h>

/* Optional 32 -> 16 kHz ADC path. Zero-initialize on each microphone open.
 * No allocation; emitted samples retain the existing unsigned 12-bit range. */
typedef struct {
    uint16_t history[64];
    unsigned at,phase;
    bool initialized,rails;
} agent_decimate_t;

/* Returns one output for each two valid inputs. Invalid arguments leave state
 * unchanged. clipped covers input rails in that interval or filter saturation. */
bool agent_decimate_feed(agent_decimate_t *,uint16_t input,uint16_t *output,bool *clipped);
#endif
