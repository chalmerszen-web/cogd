#pragma once
#include <cstddef>
#include <cstdint>
constexpr int ESP_OK=0,JPEG_ENC_ALLOC_INPUT_BUFFER=0,JPEG_ENC_ALLOC_OUTPUT_BUFFER=1;
constexpr int JPEG_ENCODE_IN_FORMAT_RGB888=0,JPEG_DOWN_SAMPLING_YUV420=2;
using jpeg_encoder_handle_t=void*;
struct jpeg_encode_memory_alloc_cfg_t {int buffer_direction=0;};
struct jpeg_encode_engine_cfg_t {int timeout_ms=0;};
struct jpeg_encode_cfg_t {unsigned width=0,height=0;int src_type=0,sub_sample=0,image_quality=0;};
void* jpeg_alloc_encoder_mem(size_t,const jpeg_encode_memory_alloc_cfg_t*,size_t*);
int jpeg_new_encoder_engine(const jpeg_encode_engine_cfg_t*,jpeg_encoder_handle_t*);
int jpeg_encoder_process(jpeg_encoder_handle_t,const jpeg_encode_cfg_t*,const void*,size_t,void*,size_t,uint32_t*);
