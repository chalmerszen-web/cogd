#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stage identifiers are a bit mask in one 320-sample diagnostic record. */
enum {
    P4_PROBE_STAGE_LINEAR = 1u,
    P4_PROBE_STAGE_FRONTEND = 2u,
    P4_PROBE_STAGE_UPLINK = 4u,
};

typedef struct {
    uint32_t hash32;
    uint32_t rms;
    uint32_t peak;
    uint32_t nonzero;
    uint32_t samples;
} p4_probe_stage_stats_t;

typedef struct {
    uint32_t stage_id;
    uint32_t start_cursor;
    uint32_t end_cursor;
    uint64_t capture_us;
    uint32_t n_samples;
    uint8_t mute_state;
    uint8_t expander_state;
    uint8_t aoq_local_accept;
    uint8_t valid;
    uint32_t aoq_start_cursor;
    uint32_t aoq_end_cursor;
    uint32_t sent_index;
    uint64_t sent_clock_us;
    uint64_t accepted_us;
    p4_probe_stage_stats_t linear;
    p4_probe_stage_stats_t frontend;
    p4_probe_stage_stats_t uplink;
} p4_probe_bound_t;

/* FNV-1a is used as a bounded diagnostic hash. It identifies a 320-sample
 * block without adding a persistent PCM buffer to the device. */
static inline uint32_t p4_probe_hash32(const int16_t *pcm, size_t samples)
{
    const uint8_t *bytes=(const uint8_t *)pcm;
    uint32_t hash=2166136261u;
    if(!pcm)return 0;
    for(size_t i=0;i<samples*sizeof(*pcm);++i){hash^=bytes[i];hash*=16777619u;}
    return hash;
}

static inline p4_probe_stage_stats_t p4_probe_stage_measure(const int16_t *pcm,size_t samples)
{
    p4_probe_stage_stats_t result={0};
    if(!pcm || !samples)return result;
    uint64_t sum=0;uint32_t peak=0,nonzero=0;
    for(size_t i=0;i<samples;++i){
        int value=pcm[i];uint32_t magnitude=(uint32_t)(value<0?-value:value);
        sum+=magnitude;
        if(magnitude>peak)peak=magnitude;
        if(value)++nonzero;
    }
    result.hash32=p4_probe_hash32(pcm,samples);
    /* The bounded probe uses mean absolute level in the rms field. It is
     * deterministic, integer-only, and avoids adding floating-point work to
     * the 20 ms frontend deadline. */
    result.rms=(uint32_t)(sum/samples);
    result.peak=peak;result.nonzero=nonzero;result.samples=(uint32_t)samples;
    (void)sum;
    return result;
}

static inline bool p4_probe_bound_complete(const p4_probe_bound_t *record)
{
    return record && record->valid && record->stage_id==
        (P4_PROBE_STAGE_LINEAR|P4_PROBE_STAGE_FRONTEND|P4_PROBE_STAGE_UPLINK) &&
        record->n_samples==320u && record->end_cursor-record->start_cursor==320u &&
        record->aoq_end_cursor-record->aoq_start_cursor==320u &&
        record->linear.samples==320u && record->frontend.samples==320u &&
        record->uplink.samples==320u;
}
