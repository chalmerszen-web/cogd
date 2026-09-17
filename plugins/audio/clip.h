#ifndef AGENT_CLIP_H
#define AGENT_CLIP_H
#include "flash.h"
#include "audio.h"
#define AGENT_CLIP_MAX_MS 10000u
/* 112 PCM samples complete the first 256-byte Flash page after its 32-byte
 * header. Following 256-sample batches then begin on a physical page boundary. */
#define AGENT_CLIP_FIRST_BATCH 112u
#ifndef AGENT_PACKED_CLIP
#define AGENT_PACKED_CLIP 0
#endif
typedef struct {
    agent_flash_ops_t flash;
    size_t samples,written,erased,erase_end;
    uint32_t crc;
    bool ready,writing;
#if AGENT_PACKED_CLIP
    bool packed;
    unsigned packed_version;
    uint32_t encoded_crc;
    size_t storage_bytes,available;
    struct { size_t flushed; unsigned used; uint8_t page[256]; } pack;
    /* Only the active reader owns these fields; join before owner reuse. */
    struct {
        size_t offset,sample;
        unsigned at,used,remaining;
        int32_t previous;
        uint32_t crc;
        uint32_t bits;
        unsigned bit_count,width;
        bool delta,first;
        uint8_t cache[256];
    } reader;
#endif
} agent_clip_t;
/* Signed 16-bit, little-endian mono PCM; one replaceable local clip. */
agent_err_t agent_clip_open(agent_clip_t *,const agent_flash_ops_t *);
agent_err_t agent_clip_begin(agent_clip_t *,unsigned ms);
/* One sector per call; capture starts only when done is true. */
agent_err_t agent_clip_prepare(agent_clip_t *,bool *done);
agent_err_t agent_clip_write(agent_clip_t *,const int16_t *,size_t samples);
agent_err_t agent_clip_commit(agent_clip_t *);
/* Commit a shorter, fully written capture (e.g. an endpointed utterance). */
agent_err_t agent_clip_finish(agent_clip_t *);
/* Owner has stopped writing. Verify the original whole writer CRC before
 * retaining a shorter prefix; errors leave no committed replacement header. */
agent_err_t agent_clip_finish_at(agent_clip_t *,size_t samples);
agent_err_t agent_clip_read(agent_clip_t *,size_t sample,int16_t *,size_t count);
/* Internal capture reader: available is a published, completely written length.
 * Flash operations must stay stable, and the owner must join readers before
 * abort/reuse/erase. This does not expose an uncommitted clip to ordinary reads. */
agent_err_t agent_clip_read_provisional(const agent_flash_ops_t *,size_t available,size_t sample,int16_t *,size_t count);
void agent_clip_abort(agent_clip_t *);
#if AGENT_PACKED_CLIP
/* Experimental version3; writes perform bounded incremental preparation. */
agent_err_t agent_clip_begin_packed(agent_clip_t *,unsigned ms);
#endif
/* Flush final buffered data before joining a source-clock reader. */
agent_err_t agent_clip_flush(agent_clip_t *);
void agent_clip_progress(const agent_clip_t *,size_t *samples,size_t *bytes);
agent_err_t agent_clip_read_pending(agent_clip_t *,size_t available,size_t bytes,size_t sample,int16_t *,size_t count);
#endif
