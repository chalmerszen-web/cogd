#ifndef KWS_VERIFY_HEAD_H
#define KWS_VERIFY_HEAD_H
#include <stdbool.h>
#include <stdint.h>

/* Experimental fixed 48 -> 16 ReLU -> 1 head. Not in normal firmware builds.
 * Input is the concatenated penultimate INT8 E/L features; no new history. */
enum { KWS_VERIFY_INPUTS = 48, KWS_VERIFY_HIDDEN = 16,
       KWS_VERIFY_BIAS_LIMIT = 1048576 };
typedef struct {
    int8_t input_weight[KWS_VERIFY_HIDDEN][KWS_VERIFY_INPUTS];
    int32_t hidden_bias[KWS_VERIFY_HIDDEN];
    int8_t output_weight[KWS_VERIFY_HIDDEN];
    int32_t output_bias;
} kws_verify_weights_t;

bool kws_verify_weights_valid(const kws_verify_weights_t *weights);
/* Validate weights once before use. Returns a signed Q8.8 logit. */
int16_t kws_verify_score(const kws_verify_weights_t *weights,
                       const int8_t features[KWS_VERIFY_INPUTS]);
#endif
