#ifndef AGENT_WAL_H
#define AGENT_WAL_H
#include "flash.h"
#include "crc.h"
typedef struct { uint64_t seq; uint32_t kind, length; size_t offset; } agent_record_t;
typedef struct {
    agent_flash_ops_t flash;
    size_t base, used, bank_size, records;
    uint64_t generation, next_seq;
    bool ready, tail_recovered;
} agent_wal_t;
typedef agent_err_t (*agent_record_fn)(void *, const agent_record_t *, const char *);
typedef bool (*agent_keep_fn)(void *, const agent_record_t *);
enum { AGENT_WAL_EVENT = 1, AGENT_WAL_SNAPSHOT = 2 };
agent_err_t agent_wal_open(agent_wal_t *, const agent_flash_ops_t *);
/* Explicitly destructive within the context partition. Never called on corruption. */
agent_err_t agent_wal_format(agent_wal_t *, const agent_flash_ops_t *);
agent_err_t agent_wal_append(agent_wal_t *, uint32_t kind, const char *, size_t, uint64_t *);
agent_err_t agent_wal_iterate(agent_wal_t *, uint64_t after, char *, size_t, agent_record_fn, void *);
agent_err_t agent_wal_read(agent_wal_t *, const agent_record_t *, char *, size_t);
agent_err_t agent_wal_snapshot_get(agent_wal_t *, char *, size_t, size_t *);
agent_err_t agent_wal_snapshot_put(agent_wal_t *, const char *, size_t);
agent_err_t agent_wal_compact(agent_wal_t *, const char *, size_t, agent_keep_fn, void *);
#endif
