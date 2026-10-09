#ifndef TEST_TLS_CLOCK_MPI_H
#define TEST_TLS_CLOCK_MPI_H
#include <stddef.h>
#define MBEDTLS_PRIVATE(member) member
typedef struct { size_t n; unsigned bits; int value; } mbedtls_mpi;
size_t mbedtls_mpi_bitlen(const mbedtls_mpi *);
#endif
