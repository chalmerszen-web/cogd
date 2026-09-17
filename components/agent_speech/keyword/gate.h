#ifndef KEYWORD_GATE_H
#define KEYWORD_GATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { KG_FRAMES=32, KG_CHANNELS=26, KG_SCALE=4096, KG_MAX_TEMPLATES=8 };
typedef struct {
    int16_t rows[KG_FRAMES][KG_CHANNELS];
    unsigned next,count;
} keyword_gate_t;

void keyword_gate_reset(keyword_gate_t *state);
bool keyword_gate_feed(keyword_gate_t *state,const int16_t row[KG_CHANNELS]);
/* Read-only operations on a full window. Output is chronological, Q12 unit
 * vectors (or zero for a constant frame). No allocation or floating point. */
bool keyword_gate_normalize(const keyword_gate_t *state,int16_t output[KG_FRAMES*KG_CHANNELS]);
/* Templates are count chronological windows emitted by normalize(). Scores
 * are Q12 angular DTW distances. The caller decides labels/rejection policy. */
bool keyword_gate_score(const keyword_gate_t *state,const int16_t *templates,size_t count,uint32_t *scores);
#ifdef KEYWORD_GATE_ACTIVITY
/* Isolated experiment: per-row Q12 activity and its Q12 square root. */
typedef struct { uint16_t level,root; } keyword_activity_t;
bool keyword_gate_activity(const keyword_gate_t *state,keyword_activity_t output[KG_FRAMES]);
bool keyword_gate_activity_score(const keyword_gate_t *state,const int16_t *templates,
    const keyword_activity_t *activity,size_t count,uint32_t *scores);
#endif
#endif
