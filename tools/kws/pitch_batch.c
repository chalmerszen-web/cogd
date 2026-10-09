/* Host-only batch driver: unchanged C logmel frontend plus exact C pitch.
 * The legacy frontend library has24-channel ABI; only its logmel is used.
 */
#include "kws.h"
#include "pitch.h"
#include <string.h>

void kws_frontend(kws_handle_t *handle,const int16_t pcm[256],kws_trace_t *trace);

int kws_pitch_frontend_batch(kws_handle_t *handle,const int16_t *pcm,size_t frames,
                            int16_t *logmel,int16_t *pitch) {
    if(!handle || !pcm || !logmel || !pitch || !frames || frames>65536) return -1;
    kws_pitch_t state;
    kws_trace_t trace;
    kws_reset(handle);kws_pitch_reset(&state);
    for(size_t i=0;i<frames;++i) {
        kws_frontend(handle,pcm+i*256,&trace);
        memcpy(logmel+i*40,trace.logmel,sizeof(trace.logmel));
        kws_pitch_features_t feature=kws_pitch_block(&state,pcm+i*256);
        pitch[i*2]=feature.frequency_q4;pitch[i*2+1]=feature.periodicity_q12;
    }
    return 0;
}
