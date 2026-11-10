#include "p4_connection_quiet.h"
#include <assert.h>
int main(void)
{
    /* candidate134 quick query26 started while the 120-second renewal
     * waited 1.68 s for ready. One second since ASR is not speaker quiet. */
    assert(!p4_connection_quiet(140826,138633,139825));
    assert(!p4_connection_quiet(145824,138633,139825));
    assert(p4_connection_quiet(145825,138633,139825));
    assert(!p4_connection_quiet(146000,141000,140000));
    assert(p4_connection_quiet(147000,141000,140000));
    assert(!p4_connection_quiet(5999,0,0));
    assert(p4_connection_quiet(6000,0,0));
    assert(!p4_connection_quiet(10000,10001,0));
    assert(!p4_connection_quiet(10000,0,10001));
    /* No 32-bit uptime truncation. */
    assert(p4_connection_quiet(UINT64_C(0x100002000),UINT64_C(0x100000000),0));
    return 0;
}
