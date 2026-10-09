#include "tls_phase_clock.h"
#include "runtime.h"
#include "esp_tls.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "mbedtls/bignum.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int task_a,task_b;
static TaskHandle_t current=&task_a;
static unsigned now=100,config_calls,handshake_calls,generate_calls,agree_calls,verify_calls;
static unsigned run_us,attribute_calls,soft_calls;
static int attribute_result;
static int result;
static esp_tls_t tls={1};
static esp_tls_cfg_t cfg={2};
static psa_key_attributes_t attributes={.id=3};
static mbedtls_svc_key_id_t key=4;
static uint8_t source[2]={5,6},output[2];
static size_t used;
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
void vTaskGetInfo(TaskHandle_t task,TaskStatus_t *s,int stack,eTaskState state)
{ assert(!task && !stack && state==eRunning);s->ulRunTimeCounter=run_us; }
uint64_t esp_agent_now(void) { return now; }
psa_status_t psa_get_key_attributes(mbedtls_svc_key_id_t id,psa_key_attributes_t *a)
{ assert(id==4);++attribute_calls;if(!attribute_result)*a=(psa_key_attributes_t){.type=0x4001,.bits=4096};return attribute_result; }
void psa_reset_key_attributes(psa_key_attributes_t *a) { memset(a,0,sizeof(*a)); }
size_t mbedtls_mpi_bitlen(const mbedtls_mpi *a) { return a->bits; }
int __wrap_mbedtls_mpi_exp_mod_soft(mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,mbedtls_mpi *);
static mbedtls_mpi x,a,e={.n=128,.bits=17},n={.n=128,.bits=4096},rr;
int mbedtls_mpi_exp_mod_soft(mbedtls_mpi *xx,const mbedtls_mpi *aa,
    const mbedtls_mpi *ee,const mbedtls_mpi *nn,mbedtls_mpi *r)
{
    assert(xx==&x && aa==&a && ee==&e && nn==&n && r==&rr);
    ++soft_calls;now+=12;run_us+=6000;xx->value=42;return result;
}
esp_err_t __wrap_esp_create_mbedtls_handle(const char *,size_t,const void *,esp_tls_t *,void *);
int __wrap_esp_mbedtls_handshake(esp_tls_t *,const esp_tls_cfg_t *);
psa_status_t __wrap_psa_generate_key(const psa_key_attributes_t *,mbedtls_svc_key_id_t *);
psa_status_t __wrap_psa_raw_key_agreement(psa_algorithm_t,mbedtls_svc_key_id_t,const uint8_t *,size_t,uint8_t *,size_t,size_t *);
psa_status_t __wrap_psa_verify_hash(mbedtls_svc_key_id_t,psa_algorithm_t,const uint8_t *,size_t,const uint8_t *,size_t);
esp_err_t esp_create_mbedtls_handle(const char *host,size_t size,const void *config,esp_tls_t *ctx,void *server)
{
    assert(!strcmp(host,"fixture") && size==7 && config==&cfg && ctx==&tls && !server);
    ++config_calls;now+=7;return result;
}
psa_status_t psa_generate_key(const psa_key_attributes_t *attr,mbedtls_svc_key_id_t *id)
{ assert(attr==&attributes && id==&key);++generate_calls;now+=11;*id=4;return result; }
psa_status_t psa_raw_key_agreement(psa_algorithm_t alg,mbedtls_svc_key_id_t id,
    const uint8_t *peer,size_t size,uint8_t *out,size_t capacity,size_t *written)
{
    assert(alg==8 && id==4 && peer==source && size==2 && out==output && capacity==2 && written==&used);
    ++agree_calls;now+=13;memcpy(out,peer,2);*written=2;return result;
}
psa_status_t psa_verify_hash(mbedtls_svc_key_id_t id,psa_algorithm_t alg,
    const uint8_t *hash,size_t size,const uint8_t *signature,size_t signature_size)
{
    assert(id==4 && alg==8 && hash==source && size==2 && signature==source && signature_size==2);
    ++verify_calls;now+=5;run_us+=2000;
    assert(__wrap_mbedtls_mpi_exp_mod_soft(&x,&a,&e,&n,&rr)==result && x.value==42);
    return result;
}
int esp_mbedtls_handshake(esp_tls_t *ctx,const esp_tls_cfg_t *config)
{
    assert(ctx==&tls && config==&cfg);++handshake_calls;
    assert(__wrap_psa_generate_key(&attributes,&key)==result);
    assert(__wrap_psa_raw_key_agreement(8,key,source,2,output,2,&used)==result);
    assert(__wrap_psa_verify_hash(key,8,source,2,source,2)==result);
    return result;
}
static void invoke(void)
{
    assert(__wrap_esp_create_mbedtls_handle("fixture",7,&cfg,&tls,NULL)==result);
    assert(__wrap_esp_mbedtls_handshake(&tls,&cfg)==result);
    assert(used==2 && !memcmp(output,source,2));
}
static void zero(void)
{
    const unsigned *phase=esp_agent_tls_phase_clock();
    for(unsigned i=0;i<TLS_PHASE_COUNT;++i)assert(!phase[i]);
    bool overflow=true;assert(!esp_agent_tls_verify_count(&overflow) && !overflow);
    assert(!esp_agent_tls_verify_detail(0));
}
int main(void)
{
    invoke();zero();assert(now==148 && attribute_calls==0);
    esp_agent_tls_phase_watch(true);current=&task_b;invoke();zero();
    assert(attribute_calls==0);
    current=&task_a;invoke();
    const unsigned *phase=esp_agent_tls_phase_clock();
    assert(phase[TLS_CONFIG_BEGIN]==196 && phase[TLS_CONFIG_END]==203);
    assert(phase[TLS_HANDSHAKE_BEGIN]==203 && phase[TLS_HANDSHAKE_END]==244);
    assert(phase[TLS_GENERATE_MS]==11 && phase[TLS_AGREE_MS]==13 && phase[TLS_VERIFY_MS]==17);
    bool overflow;assert(esp_agent_tls_verify_count(&overflow)==1 && !overflow);
    const tls_verify_detail_t *detail=esp_agent_tls_verify_detail(0);
    assert(detail->key_type==0x4001 && detail->key_bits==4096 && detail->algorithm==8);
    assert(detail->signature_bytes==2 && detail->wall_ms==17 && detail->scheduled_us==8000);
    assert(detail->soft_calls==1 && detail->soft_ms==12 && detail->soft_scheduled_us==6000);
    assert(detail->modulus_bits==4096 && detail->exponent_bits==17 && detail->exponent_limbs==128);
    assert(!detail->attributes_status && !detail->result && !esp_agent_tls_verify_detail(1));
    for(unsigned i=TLS_GENERATE_CALLS;i<TLS_PHASE_COUNT;++i)assert(phase[i]==1);
    /* WANT_READ/continuation does not reset the first timestamp. */
    assert(!__wrap_esp_mbedtls_handshake(&tls,&cfg));
    assert(phase[TLS_HANDSHAKE_BEGIN]==203 && phase[TLS_HANDSHAKE_END]==285);
    assert(phase[TLS_GENERATE_MS]==22 && phase[TLS_GENERATE_CALLS]==2);
    esp_agent_tls_phase_watch(false);unsigned before[TLS_PHASE_COUNT];memcpy(before,phase,sizeof(before));
    invoke();assert(!memcmp(before,phase,sizeof(before)));
    esp_agent_tls_phase_watch(true);zero();result=-7;invoke();
    esp_agent_tls_phase_watch(false);
    assert(phase[TLS_GENERATE_MS]==11 && phase[TLS_AGREE_CALLS]==1 && phase[TLS_VERIFY_CALLS]==1);
    assert(config_calls==5 && handshake_calls==6 && generate_calls==6 && agree_calls==6 && verify_calls==6);
    assert(soft_calls==6 && attribute_calls==3);
    /* Overflow never skips real verification; attribute failures are recorded. */
    esp_agent_tls_phase_watch(true);attribute_result=-9;
    for(unsigned i=0;i<TLS_VERIFY_CAPACITY+1;++i)assert(__wrap_psa_verify_hash(key,8,source,2,source,2)==-7);
    assert(esp_agent_tls_verify_count(&overflow)==TLS_VERIFY_CAPACITY && overflow);
    detail=esp_agent_tls_verify_detail(0);
    assert(detail->attributes_status==-9 && !detail->key_bits && detail->result==-7);
    assert(verify_calls==6+TLS_VERIFY_CAPACITY+1 && attribute_calls==3+TLS_VERIFY_CAPACITY);
    /* Nested software work from another task cannot enter the watched row. */
    esp_agent_tls_phase_watch(true);current=&task_b;
    assert(__wrap_mbedtls_mpi_exp_mod_soft(&x,&a,&e,&n,&rr)==-7);zero();
    current=&task_a;run_us=0xfffffff0U;
    assert(__wrap_psa_verify_hash(key,8,source,2,source,2)==-7);
    assert(esp_agent_tls_verify_detail(0)->scheduled_us==8000);
    esp_agent_tls_phase_watch(false);
    puts("TLS metadata: exact forwarding/results, task isolation, nested attribution, overflow, reset and timer wrap OK");
    return 0;
}
