#ifndef TEST_TLS_CLOCK_PSA_H
#define TEST_TLS_CLOCK_PSA_H
#include <stdint.h>
#include <stddef.h>
typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
typedef uint32_t mbedtls_svc_key_id_t;
typedef struct { unsigned id,type,bits; } psa_key_attributes_t;
#define PSA_KEY_ATTRIBUTES_INIT {0,0,0}
psa_status_t psa_get_key_attributes(mbedtls_svc_key_id_t,psa_key_attributes_t *);
void psa_reset_key_attributes(psa_key_attributes_t *);
static inline unsigned psa_get_key_type(const psa_key_attributes_t *a) { return a->type; }
static inline size_t psa_get_key_bits(const psa_key_attributes_t *a) { return a->bits; }
#endif
