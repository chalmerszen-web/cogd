#pragma once
#include "aoq_objects.h"
namespace aoq_wire {
// Minimal independent baseline JPEG frame. No H.264, decoding, resizing,
// capture, queue or network ownership lives in these serialization helpers.
Track jpeg_track(uint32_t width,uint32_t height,uint16_t fps=1);
bool encode_jpeg_properties(uint64_t timestamp_us,uint64_t picture,std::vector<uint8_t>&);
bool jpeg_dimensions_match(const uint8_t*,size_t,uint32_t width,uint32_t height);
bool encode_jpeg_stream(const uint8_t*,size_t,uint32_t width,uint32_t height,
                        uint64_t timestamp_us,uint64_t group,std::vector<uint8_t>&);
}
