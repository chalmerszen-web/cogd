/* Grounded signal contracts, overflow checks and reset/state isolation. */
#include "../../prototypes/kws_pitch/pitch.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const double tau = 6.2831853071795864769;
static unsigned noise_seed = 2026100379;

static int16_t noise(void) {
    noise_seed = noise_seed*1664525u+1013904223u;
    return (int16_t)((int32_t)(noise_seed >> 16)-32768);
}

static void block(int16_t *pcm, unsigned frame, double frequency,
                  double amplitude, double dc) {
    for (unsigned i = 0; i < KWS_PITCH_BLOCK; ++i) {
        double value = dc+amplitude*sin(tau*frequency*(frame*KWS_PITCH_BLOCK+i)/16000);
        pcm[i] = (int16_t)lrint(value);
    }
}

int main(void) {
    kws_pitch_t state, other;
    int16_t pcm[KWS_PITCH_BLOCK];
    double worst = 0;
    unsigned sine_frames = 0, noise_frames = 0;
    const double frequencies[] = {90,120,160,220,320,480,720};
    const double amplitudes[] = {512,4096,20000};
    for (unsigned f = 0; f < sizeof(frequencies)/sizeof(frequencies[0]); ++f) {
        for (unsigned a = 0; a < sizeof(amplitudes)/sizeof(amplitudes[0]); ++a) {
            kws_pitch_reset(&state);
            for (unsigned frame = 0; frame < 32; ++frame) {
                block(pcm, frame, frequencies[f], amplitudes[a], 3500);
                kws_pitch_features_t feature = kws_pitch_block(&state, pcm);
                if (frame < 2) {
                    assert(!feature.frequency_q4 && !feature.periodicity_q12);
                    continue;
                }
                double error = fabs(feature.frequency_q4/16.-frequencies[f]);
                assert(feature.periodicity_q12 >= 3481);
                assert(error <= fmax(2., frequencies[f]*.02));
                if (error > worst) worst = error;
                ++sine_frames;
            }
        }
    }
    kws_pitch_reset(&state);
    for (unsigned frame = 0; frame < 256; ++frame) {
        for (unsigned i = 0; i < KWS_PITCH_BLOCK; ++i) pcm[i] = noise();
        kws_pitch_features_t feature = kws_pitch_block(&state, pcm);
        assert(!feature.frequency_q4 && !feature.periodicity_q12);
        ++noise_frames;
    }
    /* Silence, high DC, Nyquist alternation and impulses must remain unvoiced. */
    for (unsigned kind = 0; kind < 4; ++kind) {
        kws_pitch_reset(&state);
        for (unsigned frame = 0; frame < 16; ++frame) {
            for (unsigned i = 0; i < KWS_PITCH_BLOCK; ++i)
                pcm[i] = kind == 0 ? 0 : kind == 1 ? INT16_MAX :
                         kind == 2 ? (i%2 ? INT16_MIN : INT16_MAX) :
                         (i == frame ? INT16_MAX : 0);
            assert(!kws_pitch_block(&state, pcm).frequency_q4);
        }
    }
    /* A completed pitch change must settle within64ms, without future samples. */
    kws_pitch_reset(&state);
    for (unsigned frame = 0; frame < 20; ++frame) {
        double frequency = frame < 10 ? 220 : 160;
        block(pcm, frame, frequency, 8000, 0);
        kws_pitch_features_t feature = kws_pitch_block(&state, pcm);
        if (frame >= 13) assert(fabs(feature.frequency_q4/16.-160) <= 2.);
    }
    /* A falling trajectory must retain its direction and actual frequency. */
    kws_pitch_reset(&state);
    int16_t first_pitch = 0, last_pitch = 0;
    for (unsigned frame = 0; frame < 40; ++frame) {
        for (unsigned i = 0; i < KWS_PITCH_BLOCK; ++i) {
            double t = (frame*KWS_PITCH_BLOCK+i)/16000.;
            pcm[i] = (int16_t)lrint(8000*sin(tau*(220*t-35*t*t)));
        }
        kws_pitch_features_t feature = kws_pitch_block(&state, pcm);
        if (frame < 3) continue;
        assert(feature.periodicity_q12 >= 3481);
        assert(fabs(feature.frequency_q4/16.-(220-70*((frame+1)*.016))) <= 6.);
        if (frame == 3) first_pitch = feature.frequency_q4;
        last_pitch = feature.frequency_q4;
    }
    assert(first_pitch-last_pitch > 35*16);
    kws_pitch_reset(&other);
    for (unsigned frame = 0; frame < 32; ++frame) {
        block(pcm, frame, 160, 4096, 0);
        (void)kws_pitch_block(&other, pcm);
    }
    kws_pitch_reset(&state);
    kws_pitch_reset(&other);
    for (unsigned frame = 0; frame < 32; ++frame) {
        block(pcm, frame, 120, 4096, 0);
        kws_pitch_features_t a = kws_pitch_block(&state, pcm);
        kws_pitch_features_t b = kws_pitch_block(&other, pcm);
        assert(a.frequency_q4 == b.frequency_q4 && a.periodicity_q12 == b.periodicity_q12);
        assert(memcmp(&state, &other, sizeof(state)) == 0);
    }
    printf("{\"complete\":true,\"sine_steady_frames\":%u,\"noise_frames\":%u,"
           "\"maximum_sine_error_hz\":%.5f,\"state_bytes\":%zu,"
           "\"no_wake_quality_claim\":true}\n", sine_frames, noise_frames, worst, sizeof(state));
    return 0;
}
