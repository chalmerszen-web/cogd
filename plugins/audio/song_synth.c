#include "song.h"
#include "audio.h"
#include <string.h>
#include "song_waves.h"

_Static_assert(sizeof(agent_song_t)<=768,"Songs stay compact, including pattern references");
_Static_assert(sizeof(agent_song_synth_t)<=256,"Music voice state stays bounded");

static uint32_t at_tick(const agent_song_synth_t *s,unsigned tick)
{ return (uint32_t)((uint64_t)tick*AGENT_AUDIO_RATE*60/(s->song->bpm*4)); }

static void tick(agent_song_synth_t *s)
{
    unsigned local=s->tick%AGENT_SONG_TICKS;
    s->segment=(uint8_t)(s->tick/AGENT_SONG_TICKS);
    const agent_song_segment_t *q=&s->song->sequence[s->segment];
    for(unsigned i=0;i<4;++i) {
        agent_song_voice_t *v=&s->voices[i];
        if(!local) { v->note=0; v->next_tick=0; v->pitch=0; }
        if(local!=v->next_tick || q->parts[i]<0) continue;
        const agent_song_pattern_t *p=&s->song->patterns[(unsigned)q->parts[i]];
        v->pitch=0;
        if(v->note>=p->count) continue;
        const agent_song_note_t *n=&s->song->notes[p->first+v->note++];
        v->next_tick=(uint8_t)(v->next_tick+n->ticks);
        v->pitch=n->pitch; v->velocity=n->velocity;
        v->age=v->phase=0; v->length=at_tick(s,s->tick+n->ticks)-s->rendered;
        v->level=1u<<24; v->low=v->high=0;
        v->release_samples=i==2?240:i==3?120:480;
        if(v->release_samples>v->length) v->release_samples=v->length;
        v->release_at=v->length*n->gate/100;
        if(v->release_at>v->length-v->release_samples) v->release_at=v->length-v->release_samples;
        v->step=i==3?(n->pitch==36?140u:180u)*178957u:agent_audio_phase_step(n->pitch);
    }
    s->tick_end=at_tick(s,s->tick+1);
}
agent_err_t agent_song_synth_init(agent_song_synth_t *s,const agent_song_t *song)
{
    if(!s) return AGENT_ERR_ARGUMENT;
    agent_err_t error=agent_song_validate(song); if(error) return error;
    memset(s,0,sizeof(*s)); s->song=song; s->samples=agent_song_samples(song);
    for(unsigned i=0;i<4;++i) s->voices[i].noise=0x719d32abu+i*0x149cb;
    tick(s); return AGENT_OK;
}
static int32_t table(unsigned kind,uint32_t phase)
{
    unsigned index=phase>>24,frac=(phase>>16)&255;
    int32_t a=song_waves[kind][index],b=song_waves[kind][(index+1)&255];
    return a+(b-a)*(int32_t)frac/256;
}
static int32_t voice(agent_song_voice_t *v,unsigned kind)
{
    if(!v->pitch || v->age>=v->release_at+v->release_samples) return 0;
    int32_t sample; unsigned decay;
    if(kind==3) {
        v->noise^=v->noise<<13; v->noise^=v->noise>>17; v->noise^=v->noise<<5;
        int32_t noise=(int32_t)(v->noise&65535)-32768;
        if(v->pitch==36) {
            sample=agent_audio_sine(v->phase); decay=11;
            v->step-=(v->step-55u*178957u)/1024;
        } else {
            v->low+=(noise-v->low)/8; v->high+=(noise-v->high)/2;
            sample=v->pitch==38?(v->high-v->low)*3/4+agent_audio_sine(v->phase)/4:(noise-v->high);
            decay=v->pitch==38?10:8;
        }
    } else {
        sample=table(kind,v->phase); decay=kind==0?13:kind==1?14:12;
    }
    v->phase+=v->step;
    unsigned amplitude=v->level>>9;
    if(kind==1) amplitude=8192+amplitude*3/4;
    v->level-=(v->level+((1u<<decay)-1))>>decay;
    unsigned attack=kind==3?24:kind==1?120:96;
    if(v->age<attack) amplitude=amplitude*v->age/attack;
    if(v->age>=v->release_at) amplitude=amplitude*(v->release_at+v->release_samples-v->age)/v->release_samples;
    ++v->age;
    return (sample*(int32_t)amplitude/32768)*(int32_t)v->velocity/127;
}
size_t agent_song_render(agent_song_synth_t *s,int16_t *pcm,size_t capacity,unsigned volume)
{
    if(!s || !s->song || !pcm || volume>100) return 0;
    static const int weights[4]={96,64,56,28};
    size_t count=0;
    while(count<capacity && s->rendered<s->samples) {
        if(s->rendered>=s->tick_end) { ++s->tick; tick(s); }
        int32_t sample=0;
        for(unsigned i=0;i<4;++i) sample+=voice(&s->voices[i],i)*weights[i];
        sample=sample/512*(int32_t)s->song->sequence[s->segment].gain/127;
        int32_t magnitude=sample<0?-sample:sample;
        /* Gentle peak knee below the existing quarter-scale speaker ceiling. */
        if(magnitude>6000) {
            magnitude=6000+(magnitude-6000)*2191/(2191+magnitude-6000);
            sample=sample<0?-magnitude:magnitude; ++s->soft_limited;
        }
        uint32_t remaining=s->samples-s->rendered-1;
        if(remaining<4800) sample=sample*(int32_t)remaining/4800;
        pcm[count++]=(int16_t)(sample*(int32_t)volume/100);
        ++s->rendered;
    }
    return count;
}
