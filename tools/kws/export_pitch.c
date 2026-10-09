/* Native PCM16LE -> two int16 features per 256 samples, using device C code. */
#include "pitch.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 3;
    FILE *output = fopen(argv[2], "wb");
    if (!output) { fclose(input); return 3; }
    kws_pitch_t state;
    kws_pitch_reset(&state);
    unsigned char raw[2*KWS_PITCH_BLOCK];
    int result = 0;
    size_t count;
    while ((count = fread(raw, 1, sizeof(raw), input)) != 0) {
        if (count != sizeof(raw)) { result = 4; break; }
        int16_t pcm[KWS_PITCH_BLOCK];
        for (unsigned i = 0; i < KWS_PITCH_BLOCK; ++i) {
            unsigned value = raw[2*i] | (unsigned)raw[2*i+1] << 8;
            pcm[i] = (int16_t)(value >= 32768 ? (int)value-65536 : (int)value);
        }
        kws_pitch_features_t feature = kws_pitch_block(&state, pcm);
        unsigned values[2] = {(unsigned)feature.frequency_q4, (unsigned)feature.periodicity_q12};
        unsigned char bytes[4];
        for (unsigned i = 0; i < 2; ++i) {
            bytes[2*i] = (unsigned char)(values[i] & 255);
            bytes[2*i+1] = (unsigned char)(values[i] >> 8);
        }
        if (fwrite(bytes, 1, sizeof(bytes), output) != sizeof(bytes)) { result = 5; break; }
    }
    if (ferror(input)) result = 5;
    if (fclose(input)) result = 5;
    if (fclose(output)) result = 5;
    return result;
}
