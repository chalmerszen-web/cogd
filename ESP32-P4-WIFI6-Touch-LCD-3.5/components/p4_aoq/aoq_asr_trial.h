#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
// Explicit operation: "asr probe <decimal TTS generation>"; output >=2048B.
// Copies only that successful TTS generation, converts <=8s of 24k PCM to 16k,
// then uses the documented Data append ASR path. No Audio media uplink, microphone
// or playback. Fresh task/Session every time; input is wiped on success/failure.
// Returns core error numbers; overall PASS additionally requires exact fixed
// text after removing only the documented punctuation/whitespace whitelist.
int p4_aoq_asr_trial_command(const char* command,char* output,size_t capacity);
void p4_aoq_asr_trial_cancel(void);
#ifdef __cplusplus
}
#endif
