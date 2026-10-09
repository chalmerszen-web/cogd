#include "pitch_encode.h"
#include <stddef.h>

bool kws_pitch_encode(const int16_t raw[2], int8_t output[2])
{
    if (!raw || !output || raw[0] < 0 || raw[1] < 0 || raw[1] > 4096) return false;
    unsigned frequency = ((unsigned)raw[0] + 64u) / 128u;
    unsigned periodicity = ((unsigned)raw[1] + 16u) / 32u;
    output[0] = (int8_t)(frequency > 127 ? 127 : frequency);
    output[1] = (int8_t)(periodicity > 127 ? 127 : periodicity);
    return true;
}
