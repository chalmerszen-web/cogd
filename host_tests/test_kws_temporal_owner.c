#include "kws_temporal_owner.h"
#include <assert.h>
#include <stdio.h>

static void future(unsigned proof, bool expected)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block) {
        bool result = kws_temporal_owner_step(&s, block == 64 || block == 65 ? 300 : -300,
            block == proof - 1 || block == proof ? 300 : -300,
            (uint64_t)block * 512, true);
        assert(result == (expected && block == proof));
    }
}

int main(void)
{
    future(73, true); /* Exactly256ms after primary65. */
    future(74, false);
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block)
        assert(kws_temporal_owner_step(&s, block == 79 || block == 80 ? 300 : -300,
            block == 77 || block == 78 ? 300 : -300,
            (uint64_t)block * 512, true) == (block == 80));
    /* A guarded block cannot retain a pending accepted owner or proof. */
    kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 80; ++block)
        assert(!kws_temporal_owner_step(&s, block == 64 || block == 65 ? 300 : -300,
            block >= 66 ? 300 : -300, (uint64_t)block * 512, block != 66));
    assert(!kws_temporal_owner_step(&s, 300, 300, 0, true));

    uint32_t rng = 2026100277;
    unsigned outputs = 0;
    for (unsigned trial = 0; trial < 32; ++trial) {
        kws_detector_t reference;
        kws_temporal_owner_reset(&s, 268); kws_detector_reset(&reference, 268);
        uint64_t end = (uint64_t)trial << 40, owner = 0, last = 0;
        for (unsigned i = 0; i < 8192; ++i) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            bool armed = (rng & 31) != 0, gap = (rng & 1023) == 0;
            end += gap ? 1024 : 512;
            if (!armed || gap) owner = 0;
            bool primary = kws_detector_step_armed(&reference, (int16_t)(rng >> 16), end, armed);
            if (primary) owner = end;
            bool final = kws_temporal_owner_step(&s, (int16_t)(rng >> 16), (int16_t)rng, end, armed);
            if (final) {
                assert(owner && end >= owner && end - owner <= 4096);
                assert(!last || end - last >= 24000 - 4096);
                owner = 0; last = end; ++outputs;
            }
            assert(s.original.votes == reference.votes);
            assert(s.original.cooldown_until == reference.cooldown_until);
        }
    }
    assert(outputs);
    printf("temporal ownership262144 blocks, expiration, disarm and clocks passed; state=%zu\n", sizeof(s));
    return 0;
}
