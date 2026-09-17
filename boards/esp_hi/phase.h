#ifndef CAPTURE_PHASE_PROFILE_H
#define CAPTURE_PHASE_PROFILE_H
#include "agent.h"
#include <stdatomic.h>
enum {
    PH_MIC,PH_READ,PH_PARSE,PH_DECIMATE,PH_METER,PH_SOURCE,PH_SPECTRAL,
    PH_FLUSH,PH_WRITE,PH_ERASE,PH_COMMIT,PH_RMS,PH_EMPTY,PH_CALIBRATION,PH_COUNT
};
_Static_assert(sizeof(unsigned)==4,"Cycle counter wrap requires unsigned32");
typedef struct { unsigned calls,cycles,maximum; } phase_counter_t;
typedef struct { unsigned clock; bool active; } phase_stamp_t;
typedef struct {
    /* Local counters are written only by the audio producer, including its
     * synchronous spectral/Flash-write callbacks. The model reader is excluded. */
    phase_counter_t local[PH_COUNT];
    bool active,overflow;
    atomic_uint epoch,published[PH_COUNT][3],published_overflow;
} phase_profile_t;
extern phase_profile_t capture_profile;
unsigned phase_clock(void);
unsigned phase_hz(void);
phase_stamp_t phase_enter(void);
void phase_leave(unsigned,phase_stamp_t);
void phase_start(void);
void phase_finish(void);
agent_err_t phase_status(char *,size_t);
#endif
