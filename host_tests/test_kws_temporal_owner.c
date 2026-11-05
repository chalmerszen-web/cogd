#include "kws_temporal_owner.h"
#include <assert.h>
#include <stdio.h>

static bool step(kws_temporal_owner_t *s, unsigned block, bool primary, bool verify, bool armed)
{
    return kws_temporal_owner_step(s, primary ? 300 : -300, verify ? 300 : -300,
        (uint64_t)block * 512, armed);
}

static void future(unsigned proof, bool expected)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block) {
        bool result = step(&s, block, block == 64 || block == 65,
            block == proof - 1 || block == proof, true);
        assert(result == (expected && block == proof));
    }
}

static void past(unsigned proof, bool expected)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block) {
        /* The two-of-three verifier also supports proof+1, with a low input. */
        bool result = step(&s, block, block == 79 || block == 80,
            block == proof - 1 || block == proof, true);
        assert(result == (expected && block == 80));
    }
}

static void rejected_then_valid(void)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block) {
        bool primary = block == 64 || block == 65 || block == 84 || block == 85;
        bool result = step(&s, block, primary, block == 84 || block == 85, true);
        assert(result == (block == 85));
        if (block < 85) assert(!s.original.cooldown_until);
        if (block == 65) assert(s.pending == 65 * 512 && s.release_required);
        if (block == 74) assert(!s.pending && !s.release_required);
    }
}

static void accepted_cooldown(bool disarm)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 120; ++block) {
        bool high = block == 64 || block == 65 || block == 84 || block == 85 ||
                    block == 112 || block == 113;
        bool result = step(&s, block, high, high, !(disarm && block == 66));
        assert(result == (block == 65 || block == 113));
        if (block >= 65 && block < 113) assert(s.original.cooldown_until == 65 * 512 + 24000);
    }
}

static void immutable_window(void)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block) {
        /* A released second primary pulse inside the pending window must
         * neither replace its anchor nor extend the auxiliary deadline. */
        bool primary = block == 64 || block == 65 || block == 69 || block == 70 ||
                       block == 90 || block == 91;
        bool verify = block == 78 || block == 79 || block == 90 || block == 91;
        assert(step(&s, block, primary, verify, true) == (block == 91));
        if (block >= 65 && block <= 73) assert(s.pending == 65 * 512);
        if (block == 74) assert(!s.pending);
        if (block < 91) assert(!s.original.cooldown_until);
    }
}

static void release_gate(bool stable)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    kws_detector_stability(&s.original, stable);
    for (unsigned block = 1; block <= 130; ++block) {
        unsigned first = stable ? 62 : 64, short_end = stable ? 68 : 67;
        unsigned release_end = stable ? 104 : 103, accepted = stable ? 108 : 105;
        bool high = (block >= first && block <= 65) ||
                    (block > short_end && block <= 100) || block > release_end;
        bool result = step(&s, block, high, block >= 75, true);
        assert(result == (block == accepted));
        if (block == 65) assert(s.pending == 65 * 512);
        if (block == 75) assert(!s.pending && s.release_required && !s.original.cooldown_until);
    }
    /* No release at all: sustained late auxiliary support cannot reopen the
     * expired window, including after the old1.5s candidate cooldown. */
    kws_temporal_owner_reset(&s, 268);
    kws_detector_stability(&s.original, stable);
    for (unsigned block = 1; block <= 180; ++block)
        assert(!step(&s, block, block >= (stable ? 62u : 64u), block >= 75, true));
}

static void lifecycle(void)
{
    kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 100; ++block) {
        bool high = block == 64 || block == 65 || block == 80 || block == 81;
        assert(step(&s, block, high, block >= 66, block != 66) == (block == 81));
        if (block == 66) assert(!s.pending && !s.last_support && !s.verify_votes &&
            !s.release_required && !s.release_blocks && !s.original.cooldown_until);
    }
    /* Accepted cooldown survives ordinary disarm, but a real sample gap
     * restarts the existing64-block warm-up and clears that clock's cooldown. */
    kws_temporal_owner_reset(&s, 268);
    for (unsigned block = 1; block <= 65; ++block)
        assert(step(&s, block, block >= 64, block >= 64, true) == (block == 65));
    for (unsigned i = 1; i <= 64; ++i) {
        uint64_t end = (uint64_t)(66 + i) * 512;
        bool hit = kws_temporal_owner_step(&s, 300, 300, end, true);
        assert(hit == (i == 64));
        if (i < 64) assert(!s.original.cooldown_until);
    }
    assert(!kws_temporal_owner_step(&s, 300, 300, 0, true));
    assert(!s.release_required && !s.release_blocks && !s.pending && !s.original.cooldown_until);
    assert(!kws_temporal_owner_step(NULL, 300, 300, 512, true));
}

static void temporal_properties(void)
{
    /* The old test asserted equality with legacy candidate-owned cooldown.
     * That contract is intentionally replaced: rejected candidates may now
     * retry after release, while every successful output owns its full1.5s.
     * Check new invariants over gaps/disarm and independent verifier support
     * history; do not assume the old event set remains a superset. */
    uint32_t rng = 2026100277; unsigned outputs = 0;
    for (unsigned trial = 0; trial < 32; ++trial) {
        kws_temporal_owner_t s; kws_temporal_owner_reset(&s, 268);
        uint64_t end = (uint64_t)trial << 40, last = 0, support = 0;
        bool recent[3] = {false}; unsigned warm = 0;
        for (unsigned i = 0; i < 8192; ++i) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            bool armed = (rng & 31) != 0, gap = (rng & 1023) == 0 && i;
            end += gap ? 1024 : 512;
            if (gap) { warm = 0; last = 0; }
            if (!armed || gap) { recent[0] = recent[1] = recent[2] = false; support = 0; }
            if (warm < 64) ++warm;
            if (armed) {
                recent[0] = recent[1]; recent[1] = recent[2]; recent[2] = (int16_t)rng >= 268;
                if (warm >= 64 && (unsigned)recent[0] + recent[1] + recent[2] >= 2) support = end;
            }
            uint64_t cooldown = gap ? 0 : s.original.cooldown_until;
            uint64_t pending = gap || !armed ? 0 : s.pending;
            bool final = kws_temporal_owner_step(&s, (int16_t)(rng >> 16), (int16_t)rng, end, armed);
            assert(s.original.cooldown_until == (final ? end + 24000 : cooldown));
            if (final) {
                uint64_t owner = pending && end - pending <= 4096 ? pending : end;
                assert(armed && warm >= 64 && end >= cooldown);
                assert(support && end - support <= 4096 && end - owner <= 4096);
                assert(!last || end - last >= 24000);
                assert(!s.pending);last = end;++outputs;
            }
            if (!armed) assert(!s.pending && !s.last_support && !s.verify_votes &&
                !s.release_required && !s.release_blocks && !s.original.votes);
            assert(s.release_blocks < (s.original.stable ? 4 : 3));
        }
    }
    assert(outputs);
    printf("temporal accepted cooldown262144 blocks, release, boundaries, disarm and clocks passed; outputs=%u; state=%zu\n",outputs,sizeof(kws_temporal_owner_t));
}

int main(void)
{
    future(73, true);future(74, false);past(71, true);past(70, false);
    rejected_then_valid();accepted_cooldown(false);accepted_cooldown(true);
    immutable_window();release_gate(false);release_gate(true);lifecycle();temporal_properties();
    return 0;
}
