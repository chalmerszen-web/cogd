#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/bignum.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned hardware_calls,public_calls;
int mbedtls_mpi_exp_mod_unsafe(mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,mbedtls_mpi *);
int mbedtls_mpi_exp_mod_soft(mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,mbedtls_mpi *);
int __real_agent_tls_public_mpi_software(mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,const mbedtls_mpi *,mbedtls_mpi *);
int __wrap_agent_tls_public_mpi_software(mbedtls_mpi *x,const mbedtls_mpi *a,
    const mbedtls_mpi *e,const mbedtls_mpi *n,mbedtls_mpi *rr)
{ ++public_calls;return __real_agent_tls_public_mpi_software(x,a,e,n,rr); }
int mbedtls_mpi_exp_mod(mbedtls_mpi *x,const mbedtls_mpi *a,
    const mbedtls_mpi *e,const mbedtls_mpi *n,mbedtls_mpi *rr)
{ ++hardware_calls;return mbedtls_mpi_exp_mod_soft(x,a,e,n,rr); }

static size_t unhex(const char *text,unsigned char *out,size_t cap)
{
    size_t size=strlen(text);assert(!(size&1) && size/2<=cap);
    for(size_t i=0;i<size/2;++i) {unsigned v;assert(sscanf(text+2*i,"%2x",&v)==1);out[i]=(unsigned char)v;}
    return size/2;
}
static void math(char kind,const char *aa,const char *ee,const char *nn)
{
    mbedtls_mpi a,e,n,x,rr;mbedtls_mpi_init(&a);mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&n);mbedtls_mpi_init(&x);mbedtls_mpi_init(&rr);
    assert(!mbedtls_mpi_read_string(&a,16,aa) && !mbedtls_mpi_read_string(&e,16,ee));
    assert(!mbedtls_mpi_read_string(&n,16,nn));
    int status=kind=='S'?mbedtls_mpi_exp_mod(&x,&a,&e,&n,&rr):mbedtls_mpi_exp_mod_unsafe(&x,&a,&e,&n,&rr);
    if(kind=='C' && !status)status=mbedtls_mpi_exp_mod_unsafe(&x,&x,&e,&n,&rr);
    char hex[4096]="";size_t used=0;if(!status)assert(!mbedtls_mpi_write_string(&x,16,hex,sizeof(hex),&used));
    printf("{\"status\":%d,\"value\":\"%s\",\"hardware\":%u,\"public\":%u}\n",status,hex,hardware_calls,public_calls);
    mbedtls_mpi_free(&a);mbedtls_mpi_free(&e);mbedtls_mpi_free(&n);mbedtls_mpi_free(&x);mbedtls_mpi_free(&rr);
}
static void verify(char kind,const char *kk,const char *hh,const char *ss)
{
    unsigned char der[2048],digest[64],signature[1024];
    size_t k=unhex(kk,der,sizeof(der)),h=unhex(hh,digest,sizeof(digest)),s=unhex(ss,signature,sizeof(signature));
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_algorithm_t alg=kind=='V'?PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256):PSA_ALG_RSA_PSS(PSA_ALG_SHA_256);
    psa_set_key_type(&attributes,PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_HASH);psa_set_key_algorithm(&attributes,alg);
    mbedtls_svc_key_id_t id=MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t status=psa_import_key(&attributes,der,k,&id);
    if(!status)status=psa_verify_hash(id,alg,digest,h,signature,s);
    printf("{\"status\":%d,\"hardware\":%u,\"public\":%u}\n",(int)status,hardware_calls,public_calls);
    assert(!psa_destroy_key(id));psa_reset_key_attributes(&attributes);
}
int main(void)
{
    _Static_assert(sizeof(mbedtls_mpi_uint)==4,"Exercise32-bit limbs");
    assert(!psa_crypto_init());
    char line[16384],a[4096],e[4096],n[4096],kind;
    while(fgets(line,sizeof(line),stdin)) {
        assert(sscanf(line,"%c %4095s %4095s %4095s",&kind,a,e,n)==4);
        hardware_calls=public_calls=0;
        if(kind=='V' || kind=='W')verify(kind,a,e,n);else math(kind,a,e,n);
    }
    mbedtls_psa_crypto_free();return 0;
}
