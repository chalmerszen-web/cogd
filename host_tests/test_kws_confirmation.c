#include "kws_confirmation.h"
#include "kws.h"
#include <assert.h>
#include <stdio.h>

static uint32_t random_state = 2026100272;
static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int main(void)
{
    kws_confirmation_t s;
    kws_confirmation_reset(&s);
    assert(!kws_confirmation_step(&s, true, false, 512, true));
    for (uint64_t end = 1024; end < 4608; end += 512)
        assert(!kws_confirmation_step(&s, false, false, end, true));
    assert(kws_confirmation_step(&s, false, true, 4608, true));
    assert(!kws_confirmation_step(&s, false, true, 5120, true));
    /* +288 ms expires; matching cannot reuse the consumed first event. */
    assert(!kws_confirmation_step(&s, true, false, 5632, false));
    assert(!kws_confirmation_step(&s, false, true, 6144, true));
    for (uint64_t end = 6656; end <= 10240; end += 512)
        assert(!kws_confirmation_step(&s, false, false, end, true));
    assert(!kws_confirmation_step(&s, true, false, 10752, true));
    /* A dropped block or backwards clock cannot retain pending evidence. */
    assert(!kws_confirmation_step(&s, false, true, 11776, true));
    assert(!kws_confirmation_step(&s, true, false, 512, true));
    assert(!kws_confirmation_step(&s, false, true, 0, true));
    assert(!kws_confirmation_step(&s, true, false, 512, true));
    assert(kws_confirmation_step(&s, false, true, 1024, true));
    assert(!s.mask);
    kws_confirmation_reset(&s);
    uint64_t clock = UINT64_C(1) << 40;
    assert(!kws_confirmation_step(&s, false, true, clock, true));
    assert(kws_confirmation_step(&s, true, false, clock + 512, true));

    /* Accepted events, warm-up, cooldown, disarm and stream gaps together.
     * Every final event consumes one accepted event in each detector, with
     * no earlier timestamp and no violation of their 1.5 s cooldown. */
    for (unsigned trial = 0; trial < 128; ++trial) {
        kws_detector_t a, b;
        kws_detector_reset(&a, 268); kws_detector_reset(&b, 268);
        kws_confirmation_reset(&s);
        uint64_t first = 0, second = 0, final = 0;
        uint64_t end = (uint64_t)trial << 40;
        for (unsigned i = 0; i < 8192; ++i) {
            uint32_t draw = random_u32();
            bool armed = (draw & 31) != 0;
            end += (draw & 1023) == 0 ? 1024 : 512;
            bool a_hit = kws_detector_step_armed(&a, (int16_t)(draw >> 16), end, armed);
            bool b_hit = kws_detector_step_armed(&b, (int16_t)draw, end, armed);
            if (a_hit) first = end;
            if (b_hit) second = end;
            if (kws_confirmation_step(&s, a_hit, b_hit, end, armed)) {
                assert(first && second && end >= first && end >= second);
                assert(end - first <= KWS_CONFIRMATION_GAP);
                assert(end - second <= KWS_CONFIRMATION_GAP);
                assert(!final || end - final >= 24000);
                final = end; first = second = 0;
            }
        }
    }
    printf("confirmation: boundaries, ownership, clocks and 1048576 detector blocks passed; state=%zu\n", sizeof(s));
    return 0;
}
