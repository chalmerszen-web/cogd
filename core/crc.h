#ifndef AGENT_CRC_H
#define AGENT_CRC_H
#include <stddef.h>
#include <stdint.h>
/* Start at UINT32_MAX and invert the final state for standard CRC-32. */
uint32_t agent_crc32_update(uint32_t state,const void *,size_t);
uint32_t agent_crc32(const void *,size_t);
#endif
