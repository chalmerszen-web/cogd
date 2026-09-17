#ifndef TEN_FUSED_SCALE_H
#define TEN_FUSED_SCALE_H
#include "binary_scale.h"
#include <limits.h>

enum { ten_combo_mode = 2 };

/* Match int64 -> binary32 RN-even rounding followed by exact binary scaling.
 * Subnormal/overflow scaling retains the original two-step path, including
 * its intermediate rounding. No arithmetic approximation or state change. */
static inline float ten_fused_scale(int64_t value,int power)
{
    if(!value) return 0.f;
    uint32_t sign=value<0?UINT32_C(0x80000000):0,mantissa;
    unsigned top;
    if(value>=INT32_MIN && value<=INT32_MAX) {
        uint32_t magnitude=(uint32_t)value;
        if(sign) magnitude=0u-magnitude;
        top=31u-(unsigned)__builtin_clz(magnitude);
        if(top<=23) mantissa=magnitude<<(23-top);
        else {
            unsigned shift=top-23;uint32_t half=1u<<(shift-1);
            mantissa=magnitude>>shift;
            uint32_t remainder=magnitude&((half<<1)-1);
            mantissa+=(remainder>half || (remainder==half && (mantissa&1)));
        }
    } else {
        uint64_t magnitude=(uint64_t)value;
        if(sign) magnitude=UINT64_C(0)-magnitude;
        uint32_t high=(uint32_t)(magnitude>>32);
        top=high?63u-(unsigned)__builtin_clz(high):31u-(unsigned)__builtin_clz((uint32_t)magnitude);
        unsigned shift=top-23;uint64_t half=UINT64_C(1)<<(shift-1);
        mantissa=(uint32_t)(magnitude>>shift);
        uint64_t remainder=magnitude&((half<<1)-1);
        mantissa+=(remainder>half || (remainder==half && (mantissa&1)));
    }
    if(mantissa==UINT32_C(0x1000000)) { mantissa>>=1;++top; }
    int64_t exponent=(int64_t)top+127+power;
    if(exponent<=0 || exponent>=255)
        return ten_power_float((float)value,ldexpf(1.f,power),power);
    uint32_t bits=sign|((uint32_t)exponent<<23)|(mantissa&UINT32_C(0x7fffff));
    float result;memcpy(&result,&bits,sizeof(result));return result;
}
#endif
