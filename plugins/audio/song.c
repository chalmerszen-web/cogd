#include "song.h"
#include "audio.h"
#include "json.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* This fixed grammar is a pull parser: no JSON node allocation per note value. */
typedef struct { const char *p; agent_err_t error; } reader_t;
static void space(reader_t *r) { while(*r->p==' ' || *r->p=='\r' || *r->p=='\n' || *r->p=='\t') ++r->p; }
static bool take(reader_t *r,char c) { space(r); if(*r->p!=c) return false; ++r->p; return true; }
static bool need(reader_t *r,char c) { if(take(r,c)) return true; r->error=AGENT_ERR_JSON; return false; }
static bool integer(reader_t *r,int low,int high,int *value)
{
    space(r); bool negative=*r->p=='-'; if(negative) ++r->p;
    if(*r->p<'0' || *r->p>'9') { r->error=AGENT_ERR_JSON; return false; }
    unsigned n=0; bool zero=*r->p=='0';
    do {
        n=n*10+(unsigned)(*r->p++-'0');
        if(n>65535) { r->error=AGENT_ERR_ARGUMENT; return false; }
        if(zero && *r->p>='0' && *r->p<='9') { r->error=AGENT_ERR_JSON; return false; }
    } while(*r->p>='0' && *r->p<='9');
    *value=negative?-(int)n:(int)n;
    if(*value<low || *value>high) { r->error=AGENT_ERR_ARGUMENT; return false; }
    return true;
}
static int key(reader_t *r)
{
    static const char *const names[]={"v","bpm","patterns","sequence"};
    char text[12]; unsigned n=0;
    if(!need(r,'"')) return -1;
    while(*r->p && *r->p!='"') {
        unsigned c=(unsigned char)*r->p++;
        /* Accept JSON's ASCII escapes in member names, including duplicate aliases. */
        if(c=='\\') {
            c=(unsigned char)*r->p++;
            if(c=='u') {
                c=0;
                for(unsigned i=0;i<4;++i) {
                    unsigned h=(unsigned char)*r->p;
                    if(!h || !isxdigit((int)h)) { r->error=AGENT_ERR_JSON; return -1; }
                    ++r->p; c=c*16+(h<='9'?h-'0':(h|32)-'a'+10);
                }
            } else if(c!='"' && c!='\\' && c!='/') { r->error=AGENT_ERR_ARGUMENT; return -1; }
        }
        if(c<32 || c>126 || n+1>=sizeof(text)) { r->error=AGENT_ERR_ARGUMENT; return -1; }
        text[n++]=(char)c;
    }
    text[n]=0;
    if(!need(r,'"') || !need(r,':')) return -1;
    for(unsigned i=0;i<4;++i) if(!strcmp(text,names[i])) return (int)i;
    r->error=AGENT_ERR_ARGUMENT; return -1;
}
static bool patterns(reader_t *r,agent_song_t *s)
{
    if(!need(r,'[')) return false;
    do {
        if(s->pattern_count==AGENT_SONG_PATTERNS) { r->error=AGENT_ERR_LIMIT; return false; }
        agent_song_pattern_t *p=&s->patterns[s->pattern_count++]; p->first=(uint8_t)s->note_count;
        if(!need(r,'[')) return false;
        do {
            if(s->note_count==AGENT_SONG_NOTES) { r->error=AGENT_ERR_LIMIT; return false; }
            agent_song_note_t *note=&s->notes[s->note_count++]; int values[4];
            if(!need(r,'[')) return false;
            for(unsigned i=0;i<4;++i) {
                if(!integer(r,i==1?1:0,i==0?96:i==1?32:i==2?127:100,&values[i]) ||
                   (i<3 && !need(r,','))) return false;
            }
            *note=(agent_song_note_t){(uint8_t)values[0],(uint8_t)values[1],(uint8_t)values[2],(uint8_t)values[3]};
            ++p->count;
            if(!need(r,']')) return false;
        } while(take(r,','));
        if(!need(r,']')) return false;
    } while(take(r,','));
    return need(r,']');
}
static bool sequence(reader_t *r,agent_song_t *s)
{
    if(!need(r,'[')) return false;
    do {
        if(s->segment_count==AGENT_SONG_SEGMENTS) { r->error=AGENT_ERR_LIMIT; return false; }
        agent_song_segment_t *p=&s->sequence[s->segment_count++]; int value;
        if(!need(r,'[')) return false;
        for(unsigned i=0;i<4;++i) {
            if(!integer(r,-1,15,&value) || !need(r,',')) return false;
            p->parts[i]=(int8_t)value;
        }
        if(!integer(r,1,127,&value) || !need(r,']')) return false;
        p->gain=(uint8_t)value;
    } while(take(r,','));
    return need(r,']');
}
agent_err_t agent_song_parse_detail(const char *json,agent_song_t *song,char *detail,size_t cap)
{
    if(detail && cap) snprintf(detail,cap,"Use v=1, bpm=60..180, <=16 patterns, <=128 notes, <=32 sequence rows/75s. Each note is [pitch,ticks,velocity,gate]; each pattern totals <=32 ticks.");
    if(!json || !song) return AGENT_ERR_ARGUMENT;
    if(strlen(json)>AGENT_ARGS_MAX) return AGENT_ERR_LIMIT;
    reader_t r={json,AGENT_OK}; agent_song_t candidate={0}; unsigned seen=0;
    if(!need(&r,'{')) return r.error;
    do {
        int k=key(&r),value;
        if(k<0) return r.error;
        if(seen&(1u<<k)) return AGENT_ERR_DUPLICATE;
        seen|=1u<<k;
        if(k<=1) {
            if(!integer(&r,k?60:1,k?180:1,&value)) return r.error;
            if(k) candidate.bpm=(uint16_t)value;
        } else if(k==2 ? !patterns(&r,&candidate) : !sequence(&r,&candidate)) return r.error;
    } while(take(&r,','));
    if(!need(&r,'}')) return r.error;
    space(&r); if(*r.p) return AGENT_ERR_JSON;
    if(seen!=15) return AGENT_ERR_ARGUMENT;
    agent_err_t error=agent_song_validate(&candidate);
    if(error && detail && cap) {
        agent_json_writer_t w;agent_json_writer_init(&w,detail,cap);
        agent_json_printf(&w,"patterns=%u, notes=%u, sequence_rows=%u. Pattern tick totals (each <=32): [",
            candidate.pattern_count,candidate.note_count,candidate.segment_count);
        for(unsigned i=0;i<candidate.pattern_count;++i) {
            unsigned ticks=0;const agent_song_pattern_t *p=&candidate.patterns[i];
            for(unsigned j=0;j<p->count;++j) ticks+=candidate.notes[p->first+j].ticks;
            agent_json_printf(&w,"%s%u",i?",":"",ticks);
        }
        agent_json_printf(&w,"]. Shorten note[1] durations: a 64-tick pattern needs EVERY 4->2,8->4,16->8. Keep pitch/velocity/gate. Do not change BPM to fix this. Otherwise rebuild with only eight 4-tick notes. Refs -1..%u; drums 0/36/38/42. Resubmit corrected JSON.",candidate.pattern_count-1);
    }
    if(!error) *song=candidate;
    return error;
}
agent_err_t agent_song_parse(const char *json,agent_song_t *song)
{ return agent_song_parse_detail(json,song,NULL,0); }
uint32_t agent_song_samples(const agent_song_t *s)
{
    return s && s->bpm?(uint32_t)((uint64_t)s->segment_count*AGENT_SONG_TICKS*AGENT_AUDIO_RATE*60/(s->bpm*4)):0;
}
agent_err_t agent_song_validate(const agent_song_t *s)
{
    if(!s || s->bpm<60 || s->bpm>180 || !s->pattern_count || !s->segment_count || !s->note_count)
        return AGENT_ERR_ARGUMENT;
    if(s->pattern_count>AGENT_SONG_PATTERNS || s->segment_count>AGENT_SONG_SEGMENTS ||
       s->note_count>AGENT_SONG_NOTES || agent_song_samples(s)>AGENT_SONG_SECONDS*AGENT_AUDIO_RATE) return AGENT_ERR_LIMIT;
    unsigned expected=0;
    for(unsigned i=0;i<s->pattern_count;++i) {
        const agent_song_pattern_t *p=&s->patterns[i]; unsigned ticks=0;
        if(!p->count || p->first!=expected || (expected+=p->count)>s->note_count) return AGENT_ERR_ARGUMENT;
        for(unsigned j=0;j<p->count;++j) {
            const agent_song_note_t *n=&s->notes[p->first+j]; ticks+=n->ticks;
            if((n->pitch && (n->pitch<36 || n->pitch>96)) || !n->ticks || n->ticks>32 ||
               n->velocity>127 || n->gate>100 || (n->pitch && (!n->velocity || !n->gate))) return AGENT_ERR_ARGUMENT;
        }
        if(ticks>AGENT_SONG_TICKS) return AGENT_ERR_LIMIT;
    }
    if(expected!=s->note_count) return AGENT_ERR_ARGUMENT;
    bool audible=false;
    for(unsigned i=0;i<s->segment_count;++i) {
        const agent_song_segment_t *q=&s->sequence[i];
        if(!q->gain || q->gain>127) return AGENT_ERR_ARGUMENT;
        for(unsigned v=0;v<4;++v) {
            int part=q->parts[v];
            if(part < -1 || part>=s->pattern_count) return AGENT_ERR_ARGUMENT;
            if(part>=0) {
                const agent_song_pattern_t *p=&s->patterns[part];
                for(unsigned j=0;j<p->count;++j) if(s->notes[p->first+j].pitch) audible=true;
            }
            if(v==3 && part>=0) {
                const agent_song_pattern_t *p=&s->patterns[part];
                for(unsigned j=0;j<p->count;++j) {
                    unsigned pitch=s->notes[p->first+j].pitch;
                    if(pitch && pitch!=36 && pitch!=38 && pitch!=42) return AGENT_ERR_ARGUMENT;
                }
            }
        }
    }
    return audible?AGENT_OK:AGENT_ERR_ARGUMENT;
}
agent_err_t agent_song_write(const agent_song_t *s,agent_write_fn write,void *ctx)
{
    agent_err_t error=agent_song_validate(s); if(error || !write) return error?error:AGENT_ERR_ARGUMENT;
    char text[96]; int n=snprintf(text,sizeof(text),"{\"v\":1,\"bpm\":%u,\"patterns\":[",s->bpm);
    error=write(ctx,text,(size_t)n);
    for(unsigned i=0;!error && i<s->pattern_count;++i) {
        error=write(ctx,i?",[":"[",i?2:1);
        const agent_song_pattern_t *p=&s->patterns[i];
        for(unsigned j=0;!error && j<p->count;++j) {
            const agent_song_note_t *a=&s->notes[p->first+j];
            n=snprintf(text,sizeof(text),"%s[%u,%u,%u,%u]",j?",":"",a->pitch,a->ticks,a->velocity,a->gate);
            error=write(ctx,text,(size_t)n);
        }
        if(!error) error=write(ctx,"]",1);
    }
    if(!error) error=write(ctx,"],\"sequence\":[",14);
    for(unsigned i=0;!error && i<s->segment_count;++i) {
        const agent_song_segment_t *q=&s->sequence[i];
        n=snprintf(text,sizeof(text),"%s[%d,%d,%d,%d,%u]",i?",":"",q->parts[0],q->parts[1],q->parts[2],q->parts[3],q->gain);
        error=write(ctx,text,(size_t)n);
    }
    return error?error:write(ctx,"]}",2);
}
