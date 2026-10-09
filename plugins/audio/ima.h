#ifndef AGENT_IMA_H
#define AGENT_IMA_H
#include <stdint.h>
/* IMA/DVI state. Caller validates index in 0..88 and code in 0..15. */
int16_t agent_ima_decode(int *predictor,int *index,unsigned code);
unsigned agent_ima_encode(int *predictor,int *index,int16_t sample);
#endif
