#ifndef TEN_BINARY_GUARDS_H
#define TEN_BINARY_GUARDS_H

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef TEN_BINARY_GUARDS
_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 &&
               FLT_MAX_EXP==128,"Requires IEEE binary32");
static inline uint32_t ten_guard_bits(float value)
{ uint32_t bits;memcpy(&bits,&value,sizeof(bits));return bits; }
static inline float ten_guard_float(uint32_t bits)
{ float value;memcpy(&value,&bits,sizeof(value));return value; }
#endif

static inline bool ten_guard_finite(float value)
{
#ifdef TEN_BINARY_GUARDS
    return (ten_guard_bits(value)&0x7fffffffu)<0x7f800000u;
#else
    return isfinite(value);
#endif
}
/* Bounds must be finite and nonnegative; NaN/infinity never pass. */
static inline bool ten_guard_abs_le(float value,float bound)
{
#ifdef TEN_BINARY_GUARDS
    return (ten_guard_bits(value)&0x7fffffffu)<=(ten_guard_bits(bound)&0x7fffffffu);
#else
    return isfinite(value) && fabsf(value)<=bound;
#endif
}
static inline bool ten_guard_abs_lt(float value,float bound)
{
#ifdef TEN_BINARY_GUARDS
    return (ten_guard_bits(value)&0x7fffffffu)<(ten_guard_bits(bound)&0x7fffffffu);
#else
    return isfinite(value) && fabsf(value)<bound;
#endif
}
/* maximum is nonnegative and not NaN. Preserve it when candidate is NaN. */
static inline float ten_guard_peak(float maximum,float candidate)
{
#ifdef TEN_BINARY_GUARDS
    uint32_t bits=ten_guard_bits(candidate)&0x7fffffffu;
    return bits<=0x7f800000u && bits>(ten_guard_bits(maximum)&0x7fffffffu)?
        ten_guard_float(bits):maximum;
#else
    return fabsf(candidate)>maximum?fabsf(candidate):maximum;
#endif
}
/* Ordered comparison, including both zeros, subnormals, infinities and NaNs.
 * Only the Boolean result is promised, not floating exception flags. */
static inline bool ten_guard_gt(float a,float b)
{
#ifdef TEN_BINARY_GUARDS
    uint32_t x=ten_guard_bits(a),y=ten_guard_bits(b);
    uint32_t ax=x&0x7fffffffu,ay=y&0x7fffffffu;
    if(ax>0x7f800000u || ay>0x7f800000u || !(ax|ay)) return false;
    x^=(0u-(x>>31))|0x80000000u;
    y^=(0u-(y>>31))|0x80000000u;
    return x>y;
#else
    return a>b;
#endif
}
static inline bool ten_guard_pcm16(float value)
{
#ifdef TEN_BINARY_GUARDS
    uint32_t bits=ten_guard_bits(value);
    return (bits&0x7fffffffu)<=ten_guard_bits(bits>>31?32768.0f:32767.0f);
#else
    return value>=-32768 && value<=32767;
#endif
}

#endif
