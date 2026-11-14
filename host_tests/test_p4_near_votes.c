#include <assert.h>
#include <stdio.h>
#include "p4_near_votes.h"
#include "p4_near_quality.h"

static void saturation_boundaries(void)
{
    int16_t pcm[3][512]={{0}};
    const unsigned positions[]={0,255,511};
    const int16_t endpoints[]={INT16_MIN,INT16_MAX};
    for(unsigned channel=0;channel<3;++channel)
        for(unsigned p=0;p<3;++p)for(unsigned e=0;e<2;++e){
            unsigned votes=0x1f,strong=3;
            p4_near_onset_t onset={100,200,true},hint={80,190,true};
            pcm[channel][positions[p]]=endpoints[e];
            /* Idle admission is unchanged, even at full scale. */
            assert(!p4_near_saturation_guard(false,pcm[0],pcm[1],pcm[2],&votes,&strong,&onset,&hint));
            assert(votes==0x1f && strong==3 && onset.first==100 && hint.first==80);
            assert(p4_near_saturation_guard(true,pcm[0],pcm[1],pcm[2],&votes,&strong,&onset,&hint));
            assert(votes==0 && strong==0 && !onset.active && !hint.active);
            assert(onset.first==0 && onset.last==0 && hint.first==0 && hint.last==0);
            pcm[channel][positions[p]]=0;
        }
    /* Adjacent representable values and very quiet speech are not floors. */
    for(unsigned channel=0;channel<3;++channel){
        pcm[channel][0]=INT16_MIN+1;pcm[channel][511]=INT16_MAX-1;
        pcm[channel][255]=channel==1?-1:1;
    }
    unsigned votes=0x1f,strong=3;
    p4_near_onset_t onset={100,200,true},hint={80,190,true};
    assert(!p4_near_saturation_guard(true,pcm[0],pcm[1],pcm[2],&votes,&strong,&onset,&hint));
    assert(votes==0x1f && strong==3 && onset.first==100 && hint.first==80);

    /* One invalid frame breaks an otherwise confirming cross-frame chain.
     * Six new clean frames can still confirm with a fresh retained onset. */
    pcm[2][100]=INT16_MAX;
    assert(p4_near_saturation_guard(true,pcm[0],pcm[1],pcm[2],&votes,&strong,&onset,&hint));
    pcm[2][100]=0;
    for(unsigned i=0;i<6;++i){
        assert(!p4_near_saturation_guard(true,pcm[0],pcm[1],pcm[2],&votes,&strong,&onset,&hint));
        votes=(votes<<1)|1;strong=(strong<<1)|1;
        p4_near_onset_observe(&onset,300+i*32,true);
        p4_near_onset_hint(&hint,300+i*32,true);
        assert(p4_near_duplex_confirm(true,votes,strong)==(i==5));
        assert(p4_near_onset_select(&onset,&hint,300+i*32)==300);
    }
}

int main(void)
{
    saturation_boundaries();
    /* Silence before any output keeps idle admission. A syllable gap and
     * playback-owner stop retain strict evidence, including clock wrap. */
    p4_near_output_tail_t tail={0};
    assert(!p4_near_output_duplex(&tail,100,false,false));
    assert(p4_near_output_duplex(&tail,100,true,false));
    assert(p4_near_output_duplex(&tail,132,false,false));
    assert(p4_near_output_duplex(&tail,164,false,true));
    assert(p4_near_output_duplex(&tail,803,false,false));
    assert(!p4_near_output_duplex(&tail,804,false,false));
    assert(!p4_near_output_duplex(&tail,805,false,false));
    tail=(p4_near_output_tail_t){0};
    assert(!p4_near_output_duplex(&tail,900,false,false));
    assert(p4_near_output_duplex(&tail,UINT32_MAX-100,true,false));
    assert(p4_near_output_duplex(&tail,538,false,false));
    assert(!p4_near_output_duplex(&tail,539,false,false));
    /* Both independent VAD paths require adaptive energy admission. */
    assert(p4_near_speech(true,false,true));
    assert(!p4_near_speech(true,false,false));
    assert(!p4_near_speech(false,true,false));
    assert(!p4_near_speech(false,false,true));
    assert(p4_near_speech(false,true,true));
    /* A VAD bridge is only evidence for a frame, never an immediate cut. */
    assert(!p4_near_duplex_confirm(p4_near_speech(false,true,true),1,1));
    /* Actual failed near-end trace at 1270895..1271373 ms: every accepted
     * frame also had strong NLP evidence. The long syllable gap defeated
     * the original 12-frame window; no energy threshold is relaxed here. */
    const unsigned accepted[16]={0,0,0,1,0,0,0,0,0,0,0,1,1,1,1,1};
    unsigned votes=0,strong=0;
    for(unsigned i=0;i<16;i++){
        votes=(votes<<1)|accepted[i];strong=(strong<<1)|accepted[i];
        assert(__builtin_popcount(votes&0xfff)<6);
        assert(p4_near_duplex_confirm(accepted[i],votes,strong)==(i==15));
    }
    /* A recorded echo vowel supplied one adjacent strong pair. Six weak
     * votes must not turn that 64 ms residue into an interruption. */
    assert(!p4_near_duplex_confirm(true,0x3f,0x3));
    /* The final recorded184 echo frame had nine eligible votes but only
     * two retained periodic votes after clipping. A DAC gap must not let
     * the three-vote idle rule override this failed duplex confirmation. */
    assert(!p4_near_duplex_confirm(true,0x1ff,0x42));
    assert(p4_near_duplex_confirm(true,0x3f,0x7));
    assert(!p4_near_duplex_confirm(false,0xffff,0xffff));
    /* Sparse transients and expired evidence cannot open the longer path. */
    assert(!p4_near_duplex_confirm(true,0x101f,0x0007));
    assert(!p4_near_duplex_confirm(true,0x1001f,0x1001f));
    assert(!p4_near_duplex_confirm(true,0x1f,0x1f));
    assert(p4_near_duplex_confirm(true,0x101f,0x1007));
    /* Captured tutor echo: six eligible frames, with two isolated strong
     * frames 285 ms apart. Both old quorum counts passed; no close pair. */
    const unsigned echo_votes[15]={0,0,0,1,1,1,0,0,0,0,0,0,1,1,1};
    const unsigned echo_strong[15]={0,0,0,1,0,0,0,0,0,0,0,0,1,0,0};
    votes=strong=0;
    for(unsigned i=0;i<15;i++){
        votes=(votes<<1)|echo_votes[i];strong=(strong<<1)|echo_strong[i];
        assert(!p4_near_duplex_confirm(echo_votes[i],votes,strong));
    }
    assert(__builtin_popcount(votes&0xfff)==6);
    assert(__builtin_popcount(strong&0xfff)==2);
    /* Captured quiet interruption with a long NLP dropout. The close pair
     * at the beginning remains valid through the bounded wide window. */
    const unsigned quiet_votes[16]={0,0,1,1,0,0,0,0,0,0,1,0,1,0,1,1};
    const unsigned quiet_strong[16]={0,0,1,1,0,0,0,0,0,0,1,0,0,0,0,1};
    votes=strong=0;
    for(unsigned i=0;i<16;i++){
        votes=(votes<<1)|quiet_votes[i];strong=(strong<<1)|quiet_strong[i];
        assert(p4_near_duplex_confirm(quiet_votes[i],votes,strong)==(i==15));
    }
    assert(!p4_near_duplex_confirm(true,0x3f,0x5));
    assert(p4_near_duplex_confirm(true,0x3f,0x15));
    assert(!p4_near_duplex_confirm(true,0x3f,0x9));
    assert(!p4_near_duplex_confirm(true,0x3f,0x18009));
    /* Aperiodic votes alone cannot confirm; the voiced frame may be an
     * earlier weak onset but must belong to the same12/16frame window. */
    assert(!p4_near_duplex_corroborated(true,0x3f,0x7,0));
    assert(p4_near_duplex_corroborated(true,0x3f,0x7,0x20));
    assert(!p4_near_duplex_corroborated(true,0x3f,0x7,0x1000));
    assert(!p4_near_duplex_corroborated(true,0x101f,0x1007,0));
    assert(p4_near_duplex_corroborated(true,0x101f,0x1007,0x1000));
    assert(!p4_near_duplex_corroborated(true,0x101f,0x1007,0x10000));
    assert(!p4_near_duplex_corroborated(false,0xffff,0xffff,0xffff));
    assert(!p4_near_duplex_corroborated(true,0x3f,0x9,0xffff));
    /* 229's first near syllables give three independent strong frames
     * separated by a short gap with ordinary evidence. */
    assert(!p4_near_duplex_corroborated(true,0x31,0x31,0x31));
    assert(p4_near_duplex_close_gap(0x31,0x31,0x31,0x31,0xe));
    assert(p4_near_duplex_close_gap(0x39,0x19,0x19,0x19,0x6));
    assert(p4_near_duplex_close_gap(0x3d,0xd,0xd,0xd,0x2));
    assert(!p4_near_duplex_close_gap(0x31,0x31,0x31,0x11,0xe));
    assert(!p4_near_duplex_close_gap(0x31,0x31,0x31,0x31,0xa));
    assert(!p4_near_duplex_close_gap(0x61,0x61,0x61,0x61,0x1e));
    assert(!p4_near_duplex_close_gap(0x91,0x91,0x91,0x91,0xe));
    assert(!p4_near_duplex_close_gap(0x811,0x811,0x811,0x811,0xe));
    assert(!p4_near_duplex_close_gap(0xc11,0xc11,0xc11,0x811,0xe));
    assert(!p4_near_duplex_close_gap(0x1031,0x1031,0x1031,0x1011,0xe));

    /* Captured weak syllable: ordinary candidates at offsets 5 and 7,
     * original strong anchors at 1 and 2, linked provisional edges at 0 and 3. */
    assert(!p4_near_duplex_corroborated(false,0xa6,0x6,0x6));
    assert(p4_near_duplex_pitch_pair(0xa6,0x6,0x6,0x6,0x9,0x9));
    assert(!p4_near_duplex_pitch_pair(0x26,0x6,0x6,0x6,0x9,0x9));
    assert(!p4_near_duplex_pitch_pair(0xa6,0x6,0x6,0x2,0x9,0x9));
    assert(!p4_near_duplex_pitch_pair(0xa6,0x6,0x6,0x6,0x8,0x9));
    assert(!p4_near_duplex_pitch_pair(0xa6,0x6,0x6,0x6,0x9,0x1));

    /* 231's held weak phrase needs the existing long-window quorum. Final
     * frame and four earlier candidates are admitted originally. */
    const unsigned original_votes=0x313u,original_strong=1u;
    const unsigned three_residual=0x1c00u;
    assert(!p4_near_duplex_corroborated(true,original_votes,original_strong,1u));
    assert(p4_near_duplex_residual_burst(original_votes,original_strong,1u,three_residual)==three_residual);
    assert(!p4_near_duplex_residual_burst(original_votes,0,1u,three_residual));
    assert(!p4_near_duplex_residual_burst(original_votes,original_strong,0,three_residual));
    assert(!p4_near_duplex_residual_burst(0x203u,original_strong,1u,three_residual));
    assert(!p4_near_duplex_residual_burst(original_votes,original_strong,1u,0x1500u));
    assert(!p4_near_duplex_residual_burst(original_votes,original_strong,1u,0xc000u));

    /* A quiet DAC interval can restore strong evidence, but it cannot
     * fill an ordinary vote, outlive the window, or confirm on its own. */
    assert(p4_near_duplex_reference_gap(true,0x7bu,0,0,0xbu)==0xbu);
    assert(!p4_near_duplex_reference_gap(true,0x3bu,0,0,0x3u));
    assert(!p4_near_duplex_reference_gap(true,0x7bu,0,0,0x7u));
    assert(!p4_near_duplex_reference_gap(true,0x7bu,0,0,0x10003u));
    assert(!p4_near_duplex_reference_gap(false,0x7bu,0,0,0xbu));

    assert(p4_near_duplex_residual_continuation(0x30u,0x30u,0x30u,0x30u,15u)==15u);
    assert(!p4_near_duplex_residual_continuation(0x30u,0x30u,0x30u,0x30u,13u));
    assert(!p4_near_duplex_residual_continuation(0x30u,0x30u,0x30u,0x10u,15u));
    assert(!p4_near_duplex_residual_continuation(0x50u,0x50u,0x50u,0x50u,15u));
    assert(!p4_near_duplex_residual_continuation(0x30u,0x30u,0x10u,0x30u,15u));
    puts("PASS near-speech windows, voiced corroboration, saturation and recovery");
}
