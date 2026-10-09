/* Match C3 limb width. This host correctness test intentionally uses no asm. */
#define MBEDTLS_HAVE_INT32
#undef MBEDTLS_HAVE_ASM
#undef MBEDTLS_AESNI_C
