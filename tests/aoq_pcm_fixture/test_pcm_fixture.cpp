#include "aoq_pcm_fixture.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

constexpr double Pi=3.14159265358979323846;
static std::vector<int16_t> convert(const std::vector<int16_t>& in){
    const auto n=in.size()*2/3;std::vector<int16_t> out(n+2,1234);size_t written=999;
    assert(p4_aoq_pcm24_to16(in.data(),in.size(),out.data()+1,n,&written)==0);
    assert(written==n&&out.front()==1234&&out.back()==1234);
    return {out.begin()+1,out.end()-1};
}
static void boundaries(){
    size_t written=777;assert(p4_aoq_pcm24_to16(nullptr,0,nullptr,0,&written)==0&&written==0);
    for(size_t n=0;n<=103;++n){auto out=convert(std::vector<int16_t>(n,12435));assert(out.size()==n*2/3);for(auto v:out)assert(v==12435);}
    for(int16_t dc:{int16_t(-32768),int16_t(-12345),int16_t(-1),int16_t(0),int16_t(1),int16_t(32767)}){
        auto out=convert(std::vector<int16_t>(151,dc));assert(std::all_of(out.begin(),out.end(),[&](int16_t v){return v==dc;}));
    }
    auto maximum=convert(std::vector<int16_t>(P4_AOQ_PCM24_MAX_SAMPLES,23456));assert(maximum.size()==P4_AOQ_PCM16_MAX_SAMPLES&&maximum.back()==23456);
    int16_t in[12]{},out[8];std::fill(out,out+8,4321);written=777;
    auto unchanged=[&]{assert(written==777&&std::all_of(out,out+8,[](int16_t v){return v==4321;}));};
    assert(p4_aoq_pcm24_to16(in,12,out,7,&written)==5);unchanged();
    assert(p4_aoq_pcm24_to16(in,P4_AOQ_PCM24_MAX_SAMPLES+1,out,8,&written)==5);unchanged();
    assert(p4_aoq_pcm24_to16(in,std::numeric_limits<size_t>::max(),out,8,&written)==5);unchanged();
    assert(p4_aoq_pcm24_to16(nullptr,12,out,8,&written)==1);unchanged();
    assert(p4_aoq_pcm24_to16(in,12,nullptr,8,&written)==1);unchanged();
    assert(p4_aoq_pcm24_to16(in,12,out,8,nullptr)==1);unchanged();
    assert(p4_aoq_pcm24_to16(in,12,out,std::numeric_limits<size_t>::max(),&written)==1);unchanged();
    auto invalid=reinterpret_cast<const int16_t*>(std::numeric_limits<uintptr_t>::max()-1);
    assert(p4_aoq_pcm24_to16(invalid,12,out,8,&written)==1);unchanged();
    auto unaligned=reinterpret_cast<const int16_t*>(reinterpret_cast<uintptr_t>(in)+1);
    assert(p4_aoq_pcm24_to16(unaligned,12,out,8,&written)==1);unchanged();
    int16_t shared[40];std::fill(shared,shared+40,2222);
    assert(p4_aoq_pcm24_to16(shared,12,shared,8,&written)==1&&written==777);
    assert(p4_aoq_pcm24_to16(shared,12,shared+8,8,&written)==1&&written==777);
    assert(p4_aoq_pcm24_to16(shared+8,12,shared,10,&written)==1&&written==777);
    assert(std::all_of(shared,shared+40,[](int16_t v){return v==2222;}));
    // Adjacent, non-overlapping buffers are permitted; counts are samples.
    assert(p4_aoq_pcm24_to16(shared,12,shared+12,8,&written)==0&&written==8);
    alignas(size_t) int16_t metadata_alias[32]{};
    assert(p4_aoq_pcm24_to16(metadata_alias,12,out,8,reinterpret_cast<size_t*>(metadata_alias))==1);
    puts("PASS DC exact incl endpoints/fullscale, floor length 0..103/8s, sentinels, invalid/overflow/unaligned/overlap transactional rejection");
}
static std::vector<int16_t> tone(double hz,size_t n=24000){
    std::vector<int16_t> result(n);for(size_t i=0;i<n;++i)result[i]=static_cast<int16_t>(std::lround(12000*std::sin(2*Pi*hz*i/24000+0.143)));return result;
}
static void tones(){
    auto out=convert(tone(1000));double sine=0,cosine=0;const size_t skip=64,n=out.size()-2*skip;
    for(size_t i=skip;i<out.size()-skip;++i){sine+=out[i]*std::sin(2*Pi*1000*i/16000+0.143);cosine+=out[i]*std::cos(2*Pi*1000*i/16000+0.143);}
    const double amplitude=2*std::hypot(sine,cosine)/n;assert(amplitude/12000>0.995&&amplitude/12000<1.005);
    double first=0,last=0;unsigned crossings=0;
    for(size_t i=skip+1;i<out.size()-skip;++i)if(out[i-1]<=0&&out[i]>0){double at=i-1-double(out[i-1])/(out[i]-out[i-1]);if(!crossings)first=at;last=at;++crossings;}
    const double frequency=(crossings-1)*16000/(last-first);assert(std::abs(frequency-1000)<0.2);
    auto stop=convert(tone(10000));double power=0;for(size_t i=skip;i<stop.size()-skip;++i)power+=double(stop[i])*stop[i];
    const double rms=std::sqrt(power/n),db=20*std::log10(std::max(rms,1e-12)/(12000/std::sqrt(2.0)));assert(db<=-60);
    printf("PASS 1kHz frequency=%.6fHz amplitude=%.3f gain=%.8f; 10kHz alias output RMS=%.6f suppression=%.2fdB (required <=-60dB)\n",frequency,amplitude,amplitude/12000,rms,db);
}
static void saturation(){
    std::vector<int16_t> step(1200);for(size_t i=0;i<step.size();++i)step[i]=((i/96)&1)?32767:-32768;
    auto out=convert(step);unsigned high=0,low=0;
    for(size_t i=64;i<out.size()-64;++i){high+=out[i]==32767;low+=out[i]==-32768;}
    assert(high>100&&low>100);
    // A rising full-scale step must not wrap the expected positive overshoot negative.
    assert(std::any_of(out.begin()+65,out.begin()+72,[](int16_t v){return v==32767;}));
    assert(std::all_of(out.begin()+65,out.begin()+72,[](int16_t v){return v>20000;}));
    assert(std::any_of(out.begin()+129,out.begin()+136,[](int16_t v){return v== -32768;}));
    assert(std::all_of(out.begin()+129,out.begin()+136,[](int16_t v){return v< -20000;}));
    puts("PASS full-scale transitions saturate, preserve sign, and do not wrap");
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);boundaries();tones();saturation();}
