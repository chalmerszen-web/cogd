#include "kws.h"
#include <limits.h>

/* Defined for negative operands and all shift counts, including INT_MIN. */
int32_t kws_requantize(int32_t value,int shift,int32_t low,int32_t high)
{
    int64_t result=value;
    if(shift>0) {
        if(shift>32) result=0;
        else {
            int64_t magnitude=result<0?-result:result;
            magnitude=(magnitude+((int64_t)1<<(shift-1)))>>shift;
            result=result<0?-magnitude:magnitude;
        }
    } else if(shift<0 && value) {
        if(shift<=-32) return value<0?low:high;
        result*=((int64_t)1<<(-shift));
    }
    if(result<low) return low;
    if(result>high) return high;
    return (int32_t)result;
}
