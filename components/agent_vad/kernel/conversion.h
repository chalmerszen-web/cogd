#ifndef TEN_CONVERSION_H
#define TEN_CONVERSION_H

#include "binary_guards.h"
#include "binary_scale.h"
#include <limits.h>

/* Preserve all int64 values; most bounded DSP outputs need only int32. */
static inline float ten_convert_float(int64_t value)
{
#ifdef TEN_NARROW_CONVERT
    if(value>=INT32_MIN && value<=INT32_MAX) return (float)(int32_t)value;
#endif
    return (float)value;
}
/* Q15 gates bound zero-initialized cells to |Q16| <= 2^31-2^17.
 * C=2^31-2^17 is exactly representable; M=2*32767^2=C+2 and
 * trunc((32767*C+M)/32768)=C. Keep the original path outside this invariant. */
static inline int64_t ten_convert_cell(float value)
{
#ifdef TEN_NARROW_CONVERT
    if(ten_guard_abs_le(value,32766)) return ten_power_i32(value,65536,16,true);
#endif
    return (int64_t)roundf(value*65536);
}

#endif
