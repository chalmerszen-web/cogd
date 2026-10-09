#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/bignum.h"
#include "soc/soc_caps.h"

int agent_tls_public_mpi_software(mbedtls_mpi *,const mbedtls_mpi *,
    const mbedtls_mpi *,const mbedtls_mpi *,mbedtls_mpi *);

/* This API already promises a PUBLIC exponent. Private RSA/DH continue to
 * use mbedtls_mpi_exp_mod and its unchanged constant-time software fallback.
 * Only unsupported public moduli take the library's existing public path. */
int mbedtls_mpi_exp_mod_unsafe(mbedtls_mpi *x,const mbedtls_mpi *a,
    const mbedtls_mpi *e,const mbedtls_mpi *n,mbedtls_mpi *rr)
{
    if(mbedtls_mpi_bitlen(n)<=SOC_RSA_MAX_BIT_LEN)
        return mbedtls_mpi_exp_mod(x,a,e,n,rr);
    return agent_tls_public_mpi_software(x,a,e,n,rr);
}
