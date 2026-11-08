#include "p4_aoq_recovery.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
int main(void)
{
    /* Recorded compound-utterance regression: by event delivery the sender
     * is 1.9 seconds beyond the cloud endpoint, already in the next clause. */
    unsigned endpoint=101760;
    assert(p4_aoq_capture_endpoint(0,101760,4460,&endpoint));
    assert(endpoint==71360);
    assert(p4_aoq_cancel_replay(104640,endpoint,102080,true,true)==71360);
    /* Recorded candidate-68 fast replay: the valid previous endpoint was
     * 3.26 s behind the reader when cancellation arrived. The old 2.2 s
     * receipt-time cap advanced it by 1.06 s, dropping seven-plus-five and
     * leaving only "how much" for the replacement session. */
    assert(p4_aoq_cancel_replay(288736,236576,286176,true,true)==236576);
    assert(p4_aoq_cancel_replay(288736,236576,0,false,true)==236576);
    /* Mapped boundaries remain authoritative across cursor wrap. The real
     * capture-ring rewind guard, not this mapping helper, rejects expiry. */
    assert(p4_aoq_cancel_replay(1600,UINT_MAX-63999,0,false,true)==UINT_MAX-63999);
    /* A new connection starts at its rewound capture cursor, not zero. */
    assert(p4_aoq_capture_endpoint(182080,78400,4720,&endpoint));
    assert(endpoint==257600);
    assert(p4_aoq_capture_endpoint(UINT_MAX-1599,3200,200,&endpoint));
    assert(endpoint==1600);
    endpoint=123;
    assert(!p4_aoq_capture_endpoint(0,16000,1001,&endpoint));
    assert(!p4_aoq_capture_endpoint(0,UINT_MAX,(double)(UINT_MAX/16u)+1,&endpoint));
    assert(!p4_aoq_capture_endpoint(0,16000,-1,&endpoint));
    assert(!p4_aoq_capture_endpoint(0,16000,0.5,&endpoint));
    assert(!p4_aoq_capture_endpoint(0,16000,NAN,&endpoint));
    assert(!p4_aoq_capture_endpoint(0,16000,INFINITY,&endpoint));
    assert(endpoint==123);
    assert(p4_aoq_capture_endpoint(1000,0,0,&endpoint));
    assert(endpoint==1000);
    /* Recorded cloud-start interruption: old 750 ms replay began at 134560
     * and lost the question before 雨伞. The mapped onset is 114240. */
    assert(p4_aoq_capture_endpoint(0,146560,7140,&endpoint));
    assert(p4_aoq_cloud_replay(146560,0,endpoint,true,101440)==112640);
    assert(p4_aoq_cloud_replay(146560,0,0,false,101440)==134560);
    assert(p4_aoq_cloud_replay(1000,0,0,true,0)==0);
    assert(p4_aoq_cloud_replay(202000,200000,200800,true,0)==200000);
    assert(p4_aoq_cloud_replay(96000,0,16000,true,0)==48000);
    assert(p4_aoq_cloud_replay(1600,UINT_MAX-3199,UINT_MAX-1599,true,0)==UINT_MAX-3199);
    /* The observed 2.8-second confirmation must retain the preceding words,
     * while short playback cannot reach back into the previous user turn. */
    assert(p4_aoq_local_replay(104800,60000)==60000);
    assert(p4_aoq_local_replay(128000,40000)==80000);
    assert(p4_aoq_local_replay(41600,40000)==40000);
    assert(p4_aoq_local_replay(40000,40000)==40000);
    assert(p4_aoq_local_replay(1600,UINT_MAX-1599)==UINT_MAX-1599);
    /* Recorded candidate-54 onset: old rewind began before the source and
     * carried old story echo. The new bound retains its full recorded phrase. */
    assert(p4_aoq_local_onset_replay(320000,260000,256940,275697)==292112);
    assert(p4_aoq_local_onset_replay(320000,315000,256940,275697)==315000);
    assert(p4_aoq_local_onset_replay(320000,260000,256940,0)==272000);
    assert(p4_aoq_local_onset_replay(320000,260000,256940,272000)==272000);
    assert(p4_aoq_local_onset_replay(320000,260000,256940,277041)==272000);
    assert(p4_aoq_local_onset_replay(320000,260000,256940,277040)==312000);
    assert(p4_aoq_local_onset_replay(16000,0,UINT_MAX-499,100)==1600);
    assert(p4_aoq_cancel_replay(48000,40000,0,false,false)==40000);
    assert(p4_aoq_cancel_replay(96000,40000,0,false,false)==60800);
    assert(p4_aoq_cancel_replay(48000,0,40000,true,false)==40000);
    assert(p4_aoq_cancel_replay(96000,0,40000,true,false)==84000);
    assert(p4_aoq_cancel_replay(320,0,0,false,false)==0);
    assert(p4_aoq_cancel_replay(320,320,0,false,false)==320);
    /* Sample counters wrap; subtraction is intentionally unsigned. */
    assert(p4_aoq_cancel_replay(1600,UINT_MAX-1599,0,false,false)==UINT_MAX-1599);
    p4_aoq_recovery_t r={0};
    for(unsigned i=0;i<3;++i)assert(p4_aoq_cancel_retry(&r,0));
    assert(!p4_aoq_cancel_retry(&r,0));assert(!p4_aoq_cancel_retry(&r,0));
    assert(p4_aoq_cancel_retry(&r,1));assert(r.cancellations==1);
    assert(p4_aoq_cancel_retry(&r,12));assert(r.cancellations==1);
    return 0;
}
