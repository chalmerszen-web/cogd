#ifndef TEN_PITCH_GUARDS_H
#define TEN_PITCH_GUARDS_H

#include "binary_guards.h"
#if defined(TEN_PITCH_GUARDS) && !defined(TEN_BINARY_GUARDS)
#error "Pitch predicates require the checked binary32 helpers"
#endif

static inline bool ten_pitch_gt(float a,float b)
{
#ifdef TEN_PITCH_GUARDS
    return ten_guard_gt(a,b);
#else
    return a>b;
#endif
}
static inline bool ten_pitch_eq(float a,float b)
{
#ifdef TEN_PITCH_GUARDS
    uint32_t x=ten_guard_bits(a),y=ten_guard_bits(b);
    uint32_t ax=x&0x7fffffffu,ay=y&0x7fffffffu;
    return ax<=0x7f800000u && ay<=0x7f800000u && (x==y || !(ax|ay));
#else
    return a==b;
#endif
}
static inline bool ten_pitch_le(float a,float b)
{
#ifdef TEN_PITCH_GUARDS
    uint32_t x=ten_guard_bits(a),y=ten_guard_bits(b);
    uint32_t ax=x&0x7fffffffu,ay=y&0x7fffffffu;
    if(ax>0x7f800000u || ay>0x7f800000u) return false;
    if(!(ax|ay)) return true;
    x^=(0u-(x>>31))|0x80000000u;
    y^=(0u-(y>>31))|0x80000000u;
    return x<=y;
#else
    return a<=b;
#endif
}
#endif
