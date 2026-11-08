#pragma once
#include <stddef.h>
#include <stdint.h>
// Only the existing camera worker calls push. Control/read calls are made by
// the reserved native AOQ network owner. Buffers are reused across sessions;
// no camera buffer or encoder buffer is borrowed by the network transport.
int p4_video_capture_start(unsigned generation);
void p4_video_capture_stop(unsigned generation);
void p4_video_capture_push(unsigned generation,const uint16_t*,unsigned width,unsigned height,uint64_t captured_us);
int p4_video_capture_copy(unsigned generation,unsigned after,uint8_t*,size_t capacity,
                          size_t* bytes,unsigned* sequence,uint64_t* captured_us);
