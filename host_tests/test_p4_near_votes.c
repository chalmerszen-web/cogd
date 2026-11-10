#include <assert.h>
#include <stdio.h>
#include "p4_near_votes.h"

int main(void)
{
    /* Linear VAD cannot admit weak/noisy residuals by itself. The original
     * NLP voice path and strict strong-energy boundary are preserved. */
    assert(p4_near_speech(true,false,0));
    assert(!p4_near_speech(false,true,620));
    assert(!p4_near_speech(false,false,2000));
    assert(p4_near_speech(false,true,621));
    /* A VAD bridge is only evidence for a frame, never an immediate cut. */
    assert(!p4_near_duplex_confirm(p4_near_speech(false,true,709),1,1));
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
    puts("PASS bounded near-speech vote windows and recorded syllable gap");
}
