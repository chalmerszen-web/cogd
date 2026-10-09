#ifndef ESP_AGENT_TLS_PHASE_CLOCK_H
#define ESP_AGENT_TLS_PHASE_CLOCK_H
#include <stdbool.h>

enum { TLS_CONFIG_BEGIN, TLS_CONFIG_END, TLS_HANDSHAKE_BEGIN, TLS_HANDSHAKE_END,
       TLS_GENERATE_MS, TLS_AGREE_MS, TLS_VERIFY_MS,
       TLS_GENERATE_CALLS, TLS_AGREE_CALLS, TLS_VERIFY_CALLS, TLS_PHASE_COUNT };
/* Diagnostic wall clocks only, never key/certificate/request material.
 * The watched transport's sole task brackets its unchanged connect call.
 * Other tasks bypass all counters. Read on that owner or after it has joined;
 * the immutable view lasts until the next watch(true). */
void esp_agent_tls_phase_watch(bool active);
const unsigned *esp_agent_tls_phase_clock(void);
enum { TLS_VERIFY_CAPACITY=8 };
typedef struct {
    unsigned algorithm,key_type,key_bits,signature_bytes,wall_ms,scheduled_us;
    unsigned soft_calls,soft_ms,soft_scheduled_us,modulus_bits,exponent_bits,exponent_limbs;
    int attributes_status,result;
} tls_verify_detail_t;
/* Scheduled time is the existing RTOS counter, sampled without stack scans.
 * It includes attributed interrupt time and omits the unfinished time slice;
 * do not label it exclusive crypto CPU time. Only public metadata is stored. */
unsigned esp_agent_tls_verify_count(bool *overflow);
const tls_verify_detail_t *esp_agent_tls_verify_detail(unsigned row);
#endif
