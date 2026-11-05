#ifndef KWS_CONFIRMATION_H
#define KWS_CONFIRMATION_H
#include <stdbool.h>
#include <stdint.h>

enum { KWS_CONFIRMATION_GAP = 4096 }; /* 256 ms at 16 kHz. */
typedef struct {
    uint64_t last_sample, pending[2];
    uint8_t mask;
} kws_confirmation_t;

void kws_confirmation_reset(kws_confirmation_t *state);
/* Inputs must be ACCEPTED events from two independent, armed detectors.
 * A match consumes both events and is emitted at the later endpoint. Each
 * detector keeps its own warm-up and cooldown. Missing blocks or disarming
 * discard pending evidence; they never manufacture a detector event. */
bool kws_confirmation_step(kws_confirmation_t *state, bool first, bool second,
                           uint64_t sample_end, bool armed);
#endif
