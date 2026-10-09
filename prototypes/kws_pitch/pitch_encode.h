#ifndef KWS_PITCH_ENCODE_H
#define KWS_PITCH_ENCODE_H
#include <stdbool.h>
#include <stdint.h>
/* Fixed v1 encoding, no fitted statistics: F0=8Hz/unit and periodicity=1/128.
 * Round positive values to nearest, then saturate to INT8's nonnegative range.
 * Invalid source values leave output unchanged. */
bool kws_pitch_encode(const int16_t raw[2], int8_t output[2]);
#endif
