#ifndef AGENT_ACK_SPOOL_H
#define AGENT_ACK_SPOOL_H
#include "flash.h"
enum { AGENT_ACK_STORAGE=96*1024, AGENT_ACK_SAMPLES=24000*6,
       AGENT_ACK_BLOCK_SAMPLES=256, AGENT_ACK_BLOCK_BYTES=140 };
/* Volatile six-second acknowledgement. Only its unused Flash tail is erased.
 * No persistent header: a reboot can never replay an old acknowledgement.
 * One writer and one reader own separate state. The caller publishes written
 * samples with release/acquire ordering AFTER successful Flash writes. */
typedef struct {
    agent_flash_ops_t flash;
    size_t base;
    struct {
        unsigned samples,published,blocks,erased,used;
        int predictor,index;
        bool sealed;
        agent_err_t error;
        uint8_t *buffer;
        unsigned capacity,staged,staged_samples;
        uint8_t block[AGENT_ACK_BLOCK_BYTES];
    } writer;
    struct {
        unsigned samples,blocks,used,count;
        int predictor,index;
        uint8_t block[AGENT_ACK_BLOCK_BYTES];
    } reader;
} agent_ack_spool_t;
/* protected_bytes is the end of the committed user recording. Reject overlap
 * before any erase. Partition layout and the recording capacity stay intact. */
agent_err_t agent_ack_spool_init(agent_ack_spool_t *,const agent_flash_ops_t *,size_t protected_bytes);
/* Optional borrowed staging memory,140..8192 bytes, bound before any samples.
 * Publish only after a whole batch commits; seal flushes and releases memory.
 * The reader never uses this memory. Unbuffered streaming remains supported. */
agent_err_t agent_ack_spool_buffer(agent_ack_spool_t *,void *,size_t);
agent_err_t agent_ack_spool_write(agent_ack_spool_t *,const int16_t *,size_t);
agent_err_t agent_ack_spool_seal(agent_ack_spool_t *);
/* Exactly count sequential samples; published is the caller's acquired bound. */
agent_err_t agent_ack_spool_read(agent_ack_spool_t *,unsigned published,int16_t *,size_t count);
#endif
