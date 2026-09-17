/* Finite bounded operands: monotone affine+ReLU commutes with max pool.
 * For negative weights choose the minimum; positive/zero choose maximum.
 * Retain the exact original multiply then add; no reassociation or FMA.
 * temp[39..76] is dead scratch before the next layer, no extra state. */
#ifndef TEN_POOL_SHARED_H
#define TEN_POOL_SHARED_H
#include "model.h"
#include "binary_guards.h"
static inline void shared_pool(ten_nn_t *s,const float *weights,const float *bias)
{
    for(unsigned x=0;x<19;++x) {
        float low=s->temp[2*x],high=low;
        for(unsigned k=1;k<3;++k) {
            float v=s->temp[2*x+k];
            if(ten_guard_gt(v,high)) high=v;
            if(ten_guard_gt(low,v)) low=v;
        }
        s->temp[39+2*x]=low;s->temp[40+2*x]=high;
    }
    for(unsigned c=0;c<16;++c) {
        unsigned which=ten_guard_gt(0,weights[c])?39:40;
        for(unsigned x=0;x<19;++x) {
            float v=s->temp[which+2*x]*weights[c]+bias[c];
            s->work[c*19+x]=ten_guard_gt(v,0)?v:0;
        }
    }
}
#endif
