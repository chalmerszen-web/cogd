#include "gate.h"
#include <string.h>

enum { BAND=8, WARP=205, INFINITY_COST=0x3fffffff };
static bool valid(const keyword_gate_t *s)
{ return s && s->next<KG_FRAMES && s->count<=KG_FRAMES; }
void keyword_gate_reset(keyword_gate_t *s) { if(s) memset(s,0,sizeof(*s)); }
bool keyword_gate_feed(keyword_gate_t *s,const int16_t row[KG_CHANNELS])
{
    if(!valid(s) || !row) return false;
    memcpy(s->rows[s->next],row,sizeof(s->rows[0]));
    s->next=(s->next+1)%KG_FRAMES;
    if(s->count<KG_FRAMES) ++s->count;
    return true;
}

static uint32_t root(uint64_t value)
{
    uint64_t result=0,bit=UINT64_C(1)<<62;
    while(bit>value) bit>>=2;
    while(bit) {
        if(value>=result+bit) { value-=result+bit; result=(result>>1)+bit; }
        else result>>=1;
        bit>>=2;
    }
    return (uint32_t)result;
}
static void columns(const keyword_gate_t *s,int32_t sums[KG_CHANNELS])
{
    memset(sums,0,sizeof(int32_t)*KG_CHANNELS);
    for(unsigned r=0;r<KG_FRAMES;++r)
        for(unsigned c=0;c<KG_CHANNELS;++c) sums[c]+=s->rows[r][c];
}
static void normalize_row(const keyword_gate_t *s,unsigned row,const int32_t sums[KG_CHANNELS],int16_t *out)
{
    int32_t centered[KG_CHANNELS],mean=0;
    const int16_t *input=s->rows[(s->next+row)%KG_FRAMES];
    for(unsigned c=0;c<KG_CHANNELS;++c) {
        centered[c]=(int32_t)input[c]*KG_FRAMES-sums[c]; mean+=centered[c];
    }
    mean/=KG_CHANNELS;
    uint64_t squares=0;
    for(unsigned c=0;c<KG_CHANNELS;++c) {
        centered[c]-=mean;
        squares+=(uint64_t)((int64_t)centered[c]*centered[c]);
    }
    /* Extra square-root precision matters for tiny nonconstant frames. The
     * worst full-int16 input still fits uint64; avoid overflowing its scaling. */
    unsigned scale=squares<=UINT64_MAX/65536u?256u:1u;
    uint32_t norm=root(squares*scale*scale);
    for(unsigned c=0;c<KG_CHANNELS;++c)
        out[c]=norm?(int16_t)((int64_t)centered[c]*KG_SCALE*scale/norm):0;
}
bool keyword_gate_normalize(const keyword_gate_t *s,int16_t output[KG_FRAMES*KG_CHANNELS])
{
    if(!valid(s) || s->count!=KG_FRAMES || !output) return false;
    int32_t sums[KG_CHANNELS]; columns(s,sums);
    for(unsigned r=0;r<KG_FRAMES;++r) normalize_row(s,r,sums,output+r*KG_CHANNELS);
    return true;
}

static bool templates_valid(const int16_t *templates,size_t count)
{
    for(size_t row=0;row<count*KG_FRAMES;++row) {
        uint32_t squares=0;
        for(unsigned c=0;c<KG_CHANNELS;++c) {
            int32_t v=templates[row*KG_CHANNELS+c];
            if(v< -KG_SCALE || v>KG_SCALE) return false;
            squares+=(uint32_t)(v*v);
        }
        if(squares && (squares<4080u*4080u || squares>4104u*4104u)) return false;
    }
    return true;
}
static uint32_t lesser(uint32_t a,uint32_t b) { return a<b?a:b; }
bool keyword_gate_score(const keyword_gate_t *s,const int16_t *templates,size_t count,uint32_t *scores)
{
    if(!valid(s) || s->count!=KG_FRAMES || !templates || !scores || !count || count>KG_MAX_TEMPLATES ||
       !templates_valid(templates,count)) return false;
    int32_t sums[KG_CHANNELS]; columns(s,sums);
    /* Share each normalized row between two templates. This halves repeated
     * normalization with 264 extra stack bytes, no resident window or heap. */
    uint32_t previous[2][KG_FRAMES+1],current[2][KG_FRAMES+1]; int16_t feature[KG_CHANNELS];
    for(size_t t=0;t<count;t+=2) {
        unsigned batch=count-t>=2?2:1;
        for(unsigned b=0;b<batch;++b) {
            for(unsigned j=0;j<=KG_FRAMES;++j) previous[b][j]=INFINITY_COST;
            previous[b][0]=0;
        }
        for(unsigned i=1;i<=KG_FRAMES;++i) {
            normalize_row(s,i-1,sums,feature);
            unsigned lo=i>BAND?i-BAND:1,hi=i+BAND<KG_FRAMES?i+BAND:KG_FRAMES;
            for(unsigned b=0;b<batch;++b) {
                for(unsigned j=0;j<=KG_FRAMES;++j) current[b][j]=INFINITY_COST;
                for(unsigned j=lo;j<=hi;++j) {
                    const int16_t *reference=templates+((t+b)*KG_FRAMES+j-1)*KG_CHANNELS;
                    int32_t dot=0;
                    for(unsigned c=0;c<KG_CHANNELS;++c) dot+=(int32_t)feature[c]*reference[c];
                    int32_t distance=KG_SCALE-dot/KG_SCALE;
                    if(distance<0) distance=0;
                    if(distance>2*KG_SCALE) distance=2*KG_SCALE;
                    current[b][j]=(uint32_t)distance+lesser(previous[b][j-1],lesser(previous[b][j]+WARP,current[b][j-1]+WARP));
                }
                memcpy(previous[b],current[b],sizeof(previous[b]));
            }
        }
        for(unsigned b=0;b<batch;++b) scores[t+b]=previous[b][KG_FRAMES]/KG_FRAMES;
    }
    return true;
}

#ifdef KEYWORD_GATE_ACTIVITY
bool keyword_gate_activity(const keyword_gate_t *s,keyword_activity_t output[KG_FRAMES])
{
    if(!valid(s) || s->count!=KG_FRAMES || !output) return false;
    int32_t levels[KG_FRAMES],sorted[KG_FRAMES];
    for(unsigned r=0;r<KG_FRAMES;++r) {
        int32_t sum=0;
        for(unsigned c=0;c<KG_CHANNELS;++c) sum+=s->rows[(s->next+r)%KG_FRAMES][c];
        levels[r]=sum;
        unsigned i=r;
        while(i && sorted[i-1]>sum) { sorted[i]=sorted[i-1]; --i; }
        sorted[i]=sum;
    }
    /* Linear percentiles at 31*0.2 and 31*0.9, in 10*channel-sum units.
     * Keep the existing 64-feature-unit minimum span without float rounding. */
    int32_t floor=8*sorted[6]+2*sorted[7],high=sorted[27]+9*sorted[28];
    int32_t span=high-floor;
    if(span<64*KG_CHANNELS*10) span=64*KG_CHANNELS*10;
    for(unsigned r=0;r<KG_FRAMES;++r) {
        int32_t above=10*levels[r]-floor;
        unsigned level=above<=0?0:above>=span?KG_SCALE:(unsigned)((int64_t)above*KG_SCALE/span);
        output[r]=(keyword_activity_t){(uint16_t)level,(uint16_t)root((uint64_t)level*KG_SCALE)};
    }
    return true;
}

bool keyword_gate_activity_score(const keyword_gate_t *s,const int16_t *templates,
    const keyword_activity_t *activity,size_t count,uint32_t *scores)
{
    if(!valid(s) || s->count!=KG_FRAMES || !templates || !activity || !scores || !count || count>KG_MAX_TEMPLATES ||
       !templates_valid(templates,count)) return false;
    for(size_t r=0;r<count*KG_FRAMES;++r) {
        uint32_t level=activity[r].level,radius=activity[r].root;
        if(level>KG_SCALE || radius>KG_SCALE || radius*radius>level*KG_SCALE ||
           (radius+1)*(radius+1)<=level*KG_SCALE) return false;
    }
    keyword_activity_t weights[KG_FRAMES];
    if(!keyword_gate_activity(s,weights)) return false;
    int32_t sums[KG_CHANNELS]; columns(s,sums);
    uint32_t previous[2][KG_FRAMES+1],current[2][KG_FRAMES+1]; int16_t feature[KG_CHANNELS];
    for(size_t t=0;t<count;t+=2) {
        unsigned batch=count-t>=2?2:1;
        for(unsigned b=0;b<batch;++b) {
            for(unsigned j=0;j<=KG_FRAMES;++j) previous[b][j]=INFINITY_COST;
            previous[b][0]=0;
        }
        for(unsigned i=1;i<=KG_FRAMES;++i) {
            normalize_row(s,i-1,sums,feature);
            unsigned lo=i>BAND?i-BAND:1,hi=i+BAND<KG_FRAMES?i+BAND:KG_FRAMES;
            for(unsigned b=0;b<batch;++b) {
                for(unsigned j=0;j<=KG_FRAMES;++j) current[b][j]=INFINITY_COST;
                for(unsigned j=lo;j<=hi;++j) {
                    const int16_t *reference=templates+((t+b)*KG_FRAMES+j-1)*KG_CHANNELS;
                    const keyword_activity_t *target=activity+(t+b)*KG_FRAMES+j-1;
                    int32_t dot=0;
                    for(unsigned c=0;c<KG_CHANNELS;++c) dot+=(int32_t)feature[c]*reference[c];
                    int32_t angular=KG_SCALE-dot/KG_SCALE;
                    if(angular<0) angular=0;
                    if(angular>2*KG_SCALE) angular=2*KG_SCALE;
                    int32_t delta=(int32_t)weights[i-1].level-target->level;
                    if(delta<0) delta=-delta;
                    uint32_t distance=(uint32_t)(((uint64_t)weights[i-1].root*target->root*(unsigned)angular)>>24)+(unsigned)delta/2;
                    current[b][j]=distance+lesser(previous[b][j-1],lesser(previous[b][j]+WARP,current[b][j-1]+WARP));
                }
                memcpy(previous[b],current[b],sizeof(previous[b]));
            }
        }
        for(unsigned b=0;b<batch;++b) scores[t+b]=previous[b][KG_FRAMES]/KG_FRAMES;
    }
    return true;
}
#endif
