#ifndef AGENT_TLS_PUBLIC_MPI_CONFIG_H
#define AGENT_TLS_PUBLIC_MPI_CONFIG_H
/* Forced into the pinned bignum.c ONLY, after its full configuration loads.
 * Retain the secret software fallback. Give its existing public-only entry a
 * private symbol so the adapter can preserve hardware for supported sizes. */
#include "tf_psa_crypto_common.h"
#if !defined(MBEDTLS_MPI_EXP_MOD_ALT) || !defined(MBEDTLS_MPI_EXP_MOD_ALT_FALLBACK)
#error Public MPI dispatch requires the pinned hardware-plus-software configuration
#endif
#undef MBEDTLS_MPI_EXP_MOD_ALT
#define mbedtls_mpi_exp_mod_unsafe agent_tls_public_mpi_software
#endif
