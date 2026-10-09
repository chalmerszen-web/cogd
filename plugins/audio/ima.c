#include "ima.h"
static const int16_t steps[89]={
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,
    60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,
    337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,
    1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,
    4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
    15289,16818,18500,20350,22385,24623,27086,29794,32767
};
static const int8_t changes[8]={-1,-1,-1,-1,2,4,6,8};
int16_t agent_ima_decode(int *predictor,int *index,unsigned code)
{
    int delta=(int)(2*(code&7u)+1u)*steps[*index]/8;
    int p=*predictor+((code&8u)?-delta:delta),i=*index+changes[code&7u];
    *predictor=p>32767?32767:p< -32768?-32768:p;
    *index=i>88?88:i<0?0:i;
    return (int16_t)*predictor;
}
unsigned agent_ima_encode(int *predictor,int *index,int16_t sample)
{
    int difference=(int)sample-*predictor;
    unsigned magnitude=(unsigned)(difference<0?-difference:difference)*4u/(unsigned)steps[*index];
    unsigned code=(difference<0?8u:0u)|(magnitude>7?7:magnitude);
    (void)agent_ima_decode(predictor,index,code);
    return code;
}
