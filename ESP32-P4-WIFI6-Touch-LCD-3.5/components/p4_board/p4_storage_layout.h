#pragma once
#include <stddef.h>
/* The final MiB of media belongs exclusively to the diagnostic journal.
 * Font and media cache offsets stay unchanged; old oversized caches are rejected. */
#define P4_DIAG_BYTES 0x100000u
static inline size_t p4_media_cache_end(size_t size) {
    return size >= 0x780000u ? size - P4_DIAG_BYTES : 0;
}
