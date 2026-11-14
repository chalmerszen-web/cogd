#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "p4_probe_stage.h"

int main(void)
{
    int16_t a[320],b[320];
    for(unsigned i=0;i<320;++i)a[i]=(int16_t)((int)i-160);
    memcpy(b,a,sizeof(a));
    p4_probe_stage_stats_t first=p4_probe_stage_measure(a,320);
    p4_probe_stage_stats_t second=p4_probe_stage_measure(b,320);
    assert(first.samples==320 && first.nonzero==319 && first.peak==160);
    assert(first.hash32==second.hash32 && first.rms==second.rms);
    p4_probe_bound_t row={
        .stage_id=P4_PROBE_STAGE_LINEAR|P4_PROBE_STAGE_FRONTEND|P4_PROBE_STAGE_UPLINK,
        .start_cursor=6400,.end_cursor=6720,.n_samples=320,.valid=1,
        .aoq_start_cursor=6400,.aoq_end_cursor=6720,
        .linear=first,.frontend=first,.uplink=first};
    assert(p4_probe_bound_complete(&row));
    row.aoq_end_cursor=7040;
    assert(!p4_probe_bound_complete(&row));
    row.aoq_end_cursor=6720;row.frontend.hash32^=1u;
    assert(p4_probe_bound_complete(&row));
    return 0;
}
