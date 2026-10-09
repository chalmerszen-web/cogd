#include "kws_owner.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    kws_owner_t s;
    kws_owner_reset(&s, 268);
    /* Both detectors have independent two-of-three evidence at65, with
     * different high-score members. Their score intersection would fail. */
    for (unsigned block = 1; block <= 65; ++block) {
        int16_t a = block == 64 || block == 65 ? 300 : -300;
        int16_t b = block == 63 || block == 65 ? 300 : -300;
        assert(kws_owner_step(&s, a, b, (uint64_t)block * 512, true) == (block == 65));
    }
    assert(s.original.cooldown_until == (uint64_t)65 * 512 + 24000);
    /* No later verification may resurrect an already vetoed original hit. */
    kws_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block)
        assert(!kws_owner_step(&s, block >= 64 ? 300 : -300,
            block >= 66 ? 300 : -300, (uint64_t)block * 512, true));
    /* A sustained early verifier does not itself enter an event cooldown. */
    kws_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 80; ++block)
        assert(kws_owner_step(&s, block >= 79 ? 300 : -300, 300,
            (uint64_t)block * 512, true) == (block == 80));

    uint32_t rng = 2026100274;
    unsigned matched = 0, original_events = 0;
    for (unsigned trial = 0; trial < 32; ++trial) {
        kws_detector_t reference;
        kws_owner_reset(&s, 268); kws_detector_reset(&reference, 268);
        uint64_t end = (uint64_t)trial << 40, last = 0;
        for (unsigned i = 0; i < 8192; ++i) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            bool armed = (rng & 31) != 0;
            end += (rng & 1023) == 0 ? 1024 : 512;
            bool accepted = kws_detector_step_armed(&reference, (int16_t)(rng >> 16), end, armed);
            bool gated = kws_owner_step(&s, (int16_t)(rng >> 16), (int16_t)rng, end, armed);
            original_events += accepted;
            if (gated) {
                assert(accepted && armed);
                assert(!last || end - last >= 24000);
                last = end; ++matched;
            }
            assert(s.original.last_sample == reference.last_sample);
            assert(s.original.cooldown_until == reference.cooldown_until);
            assert(s.original.votes == reference.votes);
        }
    }
    assert(matched && matched < original_events);
    assert(!kws_owner_step(&s, 300, 300, 0, true));
    puts("original owner: vote membership, cooldown independence and262144 blocks passed");
    return 0;
}
