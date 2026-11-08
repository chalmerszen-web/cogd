#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    P4_AOQ_AUDIO = 0,
    P4_AOQ_ASR = 1,
    P4_AOQ_TTS = 2,
    P4_AOQ_OMNI = 3
} p4_aoq_kind_t;

#define P4_AOQ_ENDPOINTS_MAX 4u
#define P4_AOQ_RESPONSE_MAX 16384u

typedef struct {
    char host[256]; /* Bare DNS name, IPv4, or IPv6; never a URL. */
    uint16_t port;
    uint32_t route_index;
} p4_aoq_endpoint_t;

/* Sensitive session material. Never log/serialize this object or store in NVS.
 * Prefer caller-owned heap storage over a small worker stack. */
typedef struct {
    char sid[256];
    char token[1024];
    uint8_t fingerprint_sha256[32];
    p4_aoq_endpoint_t endpoints[P4_AOQ_ENDPOINTS_MAX];
    size_t endpoint_count;
    char workspace[128];
    uint32_t expires_in_seconds;
    int64_t received_at_ms; /* esp_timer monotonic time; use to enforce expiry. */
    p4_aoq_kind_t kind;
} p4_aoq_allocation_t;

/* P4 only. Fixed official origin, path and model selected solely by kind.
 * Returns core agent_err_t numeric values (0 success). On every failure out is
 * zeroed. timeout_ms must be 1000..30000; response is bounded to 16 KiB.
 * No retry, redirect, NVS write, Wi-Fi setup, or audio is performed here. */
int p4_aoq_allocate(p4_aoq_kind_t kind, p4_aoq_allocation_t *out, unsigned timeout_ms);
void p4_aoq_allocation_clear(p4_aoq_allocation_t *allocation);

#ifdef __cplusplus
}
#endif
