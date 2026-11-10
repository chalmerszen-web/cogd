#ifndef P4_CONNECTION_QUIET_H
#define P4_CONNECTION_QUIET_H
#include <stdbool.h>
#include <stdint.h>
/* Renewal is maintenance, not a turn boundary. A cached response can finish
 * more than a second after ASR activity; renewing then stalls the immediately
 * following human question. Require a genuine gap after both input activity
 * and audible output, while preserving the independent provider-age check. */
static inline bool p4_connection_quiet(uint64_t now,uint64_t activity,uint64_t output_end)
{
    return now>=activity && now-activity>=6000u &&
           (!output_end || (now>=output_end && now-output_end>=6000u));
}
#endif
