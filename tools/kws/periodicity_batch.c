/* Host-only adapter for frozen PCM recipe replay. This separate library keeps
 * the old batch ABI, but produces original trusted F0 plus continuous strength.
 * Corpus/model metadata must name the new definition; old models cannot use it.
 */
#include "kws.h"
#include "periodicity.h"
#include <string.h>

void kws_frontend(kws_handle_t *handle,const int16_t pcm[256],kws_trace_t *trace);

int kws_pitch_frontend_batch(kws_handle_t *handle,const int16_t *pcm,size_t frames,
                            int16_t *logmel,int16_t *features)
{
    if (!handle || !pcm || !logmel || !features || !frames || frames>65536) return -1;
    kws_periodicity_t state;
    kws_trace_t trace;
    kws_reset(handle);kws_periodicity_reset(&state);
    for (size_t i=0;i<frames;++i) {
        kws_frontend(handle,pcm+i*256,&trace);
        memcpy(logmel+i*40,trace.logmel,sizeof(trace.logmel));
        kws_periodicity_features_t value=kws_periodicity_block(&state,pcm+i*256);
        features[i*2]=value.trusted_frequency_q4;
        features[i*2+1]=value.strength_q12;
    }
    return 0;
}
