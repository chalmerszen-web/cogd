#ifndef AGENT_BUSY_TRACE_H
#define AGENT_BUSY_TRACE_H
#include "agent.h"

enum {
    BUSY_SUBMIT, BUSY_SUBMIT_QUEUE, BUSY_NETWORK_LOCK, BUSY_NETWORK_CAPTURE,
    BUSY_DEVICE_GUARD, BUSY_DEVICE_REAP, BUSY_DEVICE_CLAIM,
    BUSY_DEVICE_RECORDING, BUSY_DEVICE_BACKEND, BUSY_DEVICE_CLEANUP,
    BUSY_AUDIO_LOCK, BUSY_AUDIO_ACTIVE, BUSY_AUDIO_QUEUE, BUSY_SITES
};
#ifdef AGENT_BUSY_TRACE
/* Diagnostic counters wrap modulo 2^32. Read after the observed command returns;
 * a snapshot during concurrent activity is not an atomic multi-counter view. */
agent_err_t busy_result(unsigned site, agent_err_t error);
bool busy_trace_snapshot(uint32_t out[BUSY_SITES]);
#else
#define busy_result(site, error) (error)
#endif
#endif
