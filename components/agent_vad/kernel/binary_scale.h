#ifndef TEN_BINARY_SCALE_H
#define TEN_BINARY_SCALE_H

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The optional path replaces only multiplication by an exact power of two.
 * multiplier must equal ldexpf(1.0f,power) and be finite and normal. Callers
 * retain finite/range checks; the integer result must fit INT32_MAX in
 * magnitude. Subnormal rescaling falls back to the C math implementation. */
static inline int32_t ten_power_i32(float value, float multiplier,
                                    int power, bool nearest)
{
#ifdef TEN_BINARY_SCALE
    (void)multiplier;
    _Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 &&
                   FLT_MAX_EXP==128, "Requires IEEE binary32");
    uint32_t bits; memcpy(&bits,&value,sizeof(bits));
    unsigned exponent=(bits>>23)&255u;
    assert(exponent!=255u);
    uint32_t mantissa=(bits&0x7fffffu)|(exponent?0x800000u:0);
    int shift=(exponent?(int)exponent-127:-126)-23+power;
    uint32_t magnitude=0;
    if(mantissa && shift>=0) {
        assert(shift<=7);
        if(shift>7) return 0;
        magnitude=mantissa<<(unsigned)shift;
    } else if(mantissa && shift>=-24) {
        unsigned right=(unsigned)-shift;
        magnitude=mantissa>>right;
        if(nearest) magnitude+=(mantissa>>(right-1))&1u;
    }
    assert(magnitude<=INT32_MAX);
    return bits>>31?-(int32_t)magnitude:(int32_t)magnitude;
#else
    (void)power;
    float scaled=value*multiplier;
    return (int32_t)(nearest?roundf(scaled):scaled);
#endif
}

static inline float ten_power_float(float value, float multiplier, int power)
{
#ifdef TEN_BINARY_SCALE
    (void)multiplier;
    uint32_t bits; memcpy(&bits,&value,sizeof(bits));
    if(!(bits&0x7fffffffu)) return value;
    unsigned exponent=(bits>>23)&255u;
    int next=(int)exponent+power;
    if(exponent>0 && exponent<255 && next>0 && next<255) {
        bits=(bits&0x807fffffu)|((uint32_t)next<<23);
        memcpy(&value,&bits,sizeof(value));
        return value;
    }
    return ldexpf(value,power);
#else
    (void)power;
    return value*multiplier;
#endif
}

#endif
