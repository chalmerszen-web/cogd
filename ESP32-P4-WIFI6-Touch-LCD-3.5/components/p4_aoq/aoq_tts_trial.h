#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Explicit paid/network operation only when called. Supported command: "tts probe".
// Requires output capacity >=2048. One fresh task/session, <=30 s including
// allocation; fixed text is "原生语音测试成功。" (test content, not a verdict).
// Receives only: no microphone, camera or playback. Returns core error number.
// PASS requires a matched task lifecycle, selected/validated nonempty PCM,
// and actual Audio EndGroup or EndTrack on that exact PCM tuple. Arbitrary FIN,
// another stream's terminal or an empty stream ending before PCM is insufficient.
int p4_aoq_tts_trial_command(const char* command,char* output,size_t capacity);

// Safe concurrent cancellation request; observed around allocation and polls.
void p4_aoq_tts_trial_cancel(void);

// A successful trial retains <=512 KiB of the fresh generation's raw PCM in
// PSRAM. A new accepted trial immediately invalidates the old cache; any failure
// retains nothing. No borrowed pointer is exported. Busy/insufficient capacity/
// wrong generation writes zero bytes. This does NOT authorize audible playback.
// The receive trial supports one PCM (group,subgroup,stream) only, with contiguous
// object IDs. Internal sample-cursor discontinuities are diagnostic only.
// It does not concatenate groups or infer multi-turn ownership.
int p4_aoq_tts_trial_copy_pcm(uint64_t expected_generation,void* destination,
                             size_t capacity,size_t* written);

#ifdef __cplusplus
}
#endif
