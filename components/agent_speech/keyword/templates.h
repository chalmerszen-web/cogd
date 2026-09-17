#ifndef KEYWORD_TEMPLATES_H
#define KEYWORD_TEMPLATES_H
#include "gate.h"
#define KG_BANK_ID "keyword-q12-adc8-v1"
enum { KG_TEMPLATE_COUNT=8, KG_POSITIVE_COUNT=4 };
extern const int16_t keyword_templates[KG_TEMPLATE_COUNT*KG_FRAMES*KG_CHANNELS];
#endif
