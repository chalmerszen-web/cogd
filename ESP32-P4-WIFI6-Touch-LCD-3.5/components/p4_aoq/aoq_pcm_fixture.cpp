#include "aoq_pcm_fixture.h"
#include <limits>

namespace {
constexpr int Argument=1,Limit=5;
constexpr size_t Taps=49;
// Reproduce with tests/aoq_pcm_fixture/generate_coefficients.py:
// interpolate 2 / decimate 3, 48k prototype fs, 97-tap sinc at 7kHz,
// Kaiser beta 8.6. Each Q15 phase sums to 32768 exactly. Centered sampling
// removes group delay for this whole-buffer API; endpoint samples are extended.
constexpr int16_t Coefficients[2][Taps]={
    { // phase 0, offsets -24..24
        0, -2, 2, 6, -13, -6, 39,
        -15, -74, 85, 83, -215, 0, 374,
        -251, -455, 712, 272, -1345, 452, 2017,
        -2307, -2535, 10005, 19110, 10005, -2535, -2307,
        2017, 452, -1345, 272, 712, -455, -251,
        374, 0, -215, 83, 85, -74, -15,
        39, -6, -13, 6, 2, -2, 0
    },
    { // phase 1, offsets -24..24
        0, -1, -1, 6, -2, -18, 19,
        29, -65, -13, 140, -74, -204, 269,
        169, -564, 95, 858, -722, -931, 1856,
        357, -3959, 2619, 16521, 16521, 2619, -3959,
        357, 1856, -931, -722, 858, 95, -564,
        169, 269, -204, -74, 140, -13, -65,
        29, 19, -18, -2, 6, -1, -1
    }
};
constexpr int phase_sum(size_t phase){
    int sum=0;for(size_t i=0;i<Taps;++i)sum+=Coefficients[phase][i];return sum;
}
static_assert(phase_sum(0)==32768&&phase_sum(1)==32768,"Q15 DC normalization");

struct Range {uintptr_t begin=0,end=0;};
bool range(const void* p,size_t count,size_t width,size_t alignment,Range& out){
    const auto start=reinterpret_cast<uintptr_t>(p);
    const auto max=std::numeric_limits<uintptr_t>::max();
    if((count&&!p)||start%alignment||count>max/width)return false;
    const auto bytes=static_cast<uintptr_t>(count*width);
    if(start>max-bytes)return false;
    out={start,start+bytes};return true;
}
bool overlap(const Range& a,const Range& b){
    return a.begin<a.end&&b.begin<b.end&&a.begin<b.end&&b.begin<a.end;
}
int16_t quantize(int64_t sum){
    // Symmetric rounding without implementation-defined negative right shift.
    const int64_t rounded=sum>=0?(sum+16384)/32768:-((-sum+16384)/32768);
    if(rounded>32767)return 32767;
    if(rounded< -32768)return -32768;
    return static_cast<int16_t>(rounded);
}
}

extern "C" int p4_aoq_pcm24_to16(const int16_t* input,size_t samples,
                                int16_t* output,size_t capacity,size_t* written){
    if(samples>P4_AOQ_PCM24_MAX_SAMPLES)return Limit;
    const size_t count=samples*2/3; // samples is bounded before multiplication
    if(capacity<count)return Limit;
    Range source,destination,result;
    if(!range(input,samples,sizeof(*input),alignof(int16_t),source)||
       !range(output,capacity,sizeof(*output),alignof(int16_t),destination)||
       !range(written,1,sizeof(*written),alignof(size_t),result)||
       overlap(source,destination)||overlap(source,result)||overlap(destination,result))return Argument;
    for(size_t m=0;m<count;++m){
        const int32_t base=static_cast<int32_t>((m*3)/2);
        const auto* coefficients=Coefficients[m&1];
        int64_t sum=0;
        for(size_t tap=0;tap<Taps;++tap){
            int32_t index=base+static_cast<int32_t>(tap)-24;
            if(index<0)index=0;
            else if(static_cast<size_t>(index)>=samples)index=static_cast<int32_t>(samples-1);
            // Each 16x16 product fits int32; accumulation uses int64 for ringing.
            sum+=static_cast<int32_t>(input[index])*static_cast<int32_t>(coefficients[tap]);
        }
        output[m]=quantize(sum);
    }
    *written=count;return 0;
}
