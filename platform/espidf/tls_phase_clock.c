#include "tls_phase_clock.h"
#include "runtime.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "mbedtls/bignum.h"
#include <stdatomic.h>
#include <string.h>

static _Atomic(TaskHandle_t) watched_task;
static unsigned clocks[TLS_PHASE_COUNT];
static tls_verify_detail_t verifies[TLS_VERIFY_CAPACITY],*active_verify;
static unsigned verify_count;
static bool verify_overflow;
_Static_assert(sizeof(verifies)<=1024,"Bound diagnostic public metadata");
#if !CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS || !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#error TLS phase trace requires the existing microsecond task runtime counter
#endif

static unsigned scheduled_us(void)
{
    TaskStatus_t status;
    vTaskGetInfo(NULL,&status,pdFALSE,eRunning);
    return (unsigned)status.ulRunTimeCounter;
}

static bool watching(void)
{
    TaskHandle_t task=atomic_load_explicit(&watched_task,memory_order_acquire);
    return task && task==xTaskGetCurrentTaskHandle();
}
void esp_agent_tls_phase_watch(bool active)
{
    if(active) {
        memset(clocks,0,sizeof(clocks));memset(verifies,0,sizeof(verifies));
        verify_count=0;verify_overflow=false;active_verify=NULL;
    }
    atomic_store_explicit(&watched_task,active?xTaskGetCurrentTaskHandle():NULL,memory_order_release);
}
const unsigned *esp_agent_tls_phase_clock(void) { return clocks; }
unsigned esp_agent_tls_verify_count(bool *overflow)
{ *overflow=verify_overflow;return verify_count; }
const tls_verify_detail_t *esp_agent_tls_verify_detail(unsigned row)
{ return row<verify_count?&verifies[row]:NULL; }

/* Pinned IDF esp-tls entrypoints. Each wrapper forwards identical arguments
 * exactly once. No TLS settings, retries, allocations or I/O are added. */
esp_err_t __real_esp_create_mbedtls_handle(const char *,size_t,const void *,esp_tls_t *,void *);
esp_err_t __wrap_esp_create_mbedtls_handle(const char *host,size_t size,const void *cfg,esp_tls_t *tls,void *server)
{
    bool traced=watching();if(traced)clocks[TLS_CONFIG_BEGIN]=(unsigned)esp_agent_now();
    esp_err_t result=__real_esp_create_mbedtls_handle(host,size,cfg,tls,server);
    if(traced)clocks[TLS_CONFIG_END]=(unsigned)esp_agent_now();
    return result;
}
int __real_esp_mbedtls_handshake(esp_tls_t *,const esp_tls_cfg_t *);
int __wrap_esp_mbedtls_handshake(esp_tls_t *tls,const esp_tls_cfg_t *cfg)
{
    bool traced=watching();
    if(traced && !clocks[TLS_HANDSHAKE_BEGIN])clocks[TLS_HANDSHAKE_BEGIN]=(unsigned)esp_agent_now();
    int result=__real_esp_mbedtls_handshake(tls,cfg);
    if(traced)clocks[TLS_HANDSHAKE_END]=(unsigned)esp_agent_now();
    return result;
}
static void elapsed(unsigned index,unsigned began)
{
    clocks[index]+=(unsigned)esp_agent_now()-began;
    ++clocks[index+TLS_GENERATE_CALLS-TLS_GENERATE_MS];
}
psa_status_t __real_psa_generate_key(const psa_key_attributes_t *,mbedtls_svc_key_id_t *);
psa_status_t __wrap_psa_generate_key(const psa_key_attributes_t *attributes,mbedtls_svc_key_id_t *key)
{
    bool traced=watching();unsigned began=traced?(unsigned)esp_agent_now():0;
    psa_status_t result=__real_psa_generate_key(attributes,key);
    if(traced)elapsed(TLS_GENERATE_MS,began);
    return result;
}
psa_status_t __real_psa_raw_key_agreement(psa_algorithm_t,mbedtls_svc_key_id_t,const uint8_t *,size_t,uint8_t *,size_t,size_t *);
psa_status_t __wrap_psa_raw_key_agreement(psa_algorithm_t alg,mbedtls_svc_key_id_t key,
    const uint8_t *peer,size_t size,uint8_t *out,size_t capacity,size_t *used)
{
    bool traced=watching();unsigned began=traced?(unsigned)esp_agent_now():0;
    psa_status_t result=__real_psa_raw_key_agreement(alg,key,peer,size,out,capacity,used);
    if(traced)elapsed(TLS_AGREE_MS,began);
    return result;
}
psa_status_t __real_psa_verify_hash(mbedtls_svc_key_id_t,psa_algorithm_t,const uint8_t *,size_t,const uint8_t *,size_t);
psa_status_t __wrap_psa_verify_hash(mbedtls_svc_key_id_t key,psa_algorithm_t alg,
    const uint8_t *hash,size_t size,const uint8_t *signature,size_t signature_size)
{
    if(!watching())return __real_psa_verify_hash(key,alg,hash,size,signature,signature_size);
    tls_verify_detail_t *row=NULL,*previous=active_verify;
    if(verify_count<TLS_VERIFY_CAPACITY) {
        row=&verifies[verify_count++];row->algorithm=(unsigned)alg;
        row->signature_bytes=(unsigned)signature_size;
        psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
        row->attributes_status=(int)psa_get_key_attributes(key,&attributes);
        if(!row->attributes_status) {
            row->key_bits=(unsigned)psa_get_key_bits(&attributes);
            row->key_type=(unsigned)psa_get_key_type(&attributes);
        }
        psa_reset_key_attributes(&attributes);
    } else verify_overflow=true;
    active_verify=row;
    unsigned ran=scheduled_us(),began=(unsigned)esp_agent_now();
    psa_status_t result=__real_psa_verify_hash(key,alg,hash,size,signature,signature_size);
    elapsed(TLS_VERIFY_MS,began);
    if(row) {
        row->wall_ms=(unsigned)esp_agent_now()-began;
        row->scheduled_us=scheduled_us()-ran;row->result=(int)result;
    }
    active_verify=previous;
    return result;
}

int __real_mbedtls_mpi_exp_mod_soft(mbedtls_mpi *,const mbedtls_mpi *,
    const mbedtls_mpi *,const mbedtls_mpi *,mbedtls_mpi *);
int __wrap_mbedtls_mpi_exp_mod_soft(mbedtls_mpi *x,const mbedtls_mpi *a,
    const mbedtls_mpi *e,const mbedtls_mpi *n,mbedtls_mpi *rr)
{
    tls_verify_detail_t *row=watching()?active_verify:NULL;
    unsigned began=0,ran=0;
    if(row) {
        ++row->soft_calls;row->modulus_bits=(unsigned)mbedtls_mpi_bitlen(n);
        row->exponent_bits=(unsigned)mbedtls_mpi_bitlen(e);
        row->exponent_limbs=(unsigned)e->MBEDTLS_PRIVATE(n);
        ran=scheduled_us();began=(unsigned)esp_agent_now();
    }
    int result=__real_mbedtls_mpi_exp_mod_soft(x,a,e,n,rr);
    if(row) {row->soft_ms+=(unsigned)esp_agent_now()-began;row->soft_scheduled_us+=scheduled_us()-ran;}
    return result;
}
