#pragma once
#include <cstddef>
#include <cstdint>
inline uint32_t esp_random(){static uint32_t next=10;return ++next;}
inline void esp_fill_random(void* p,size_t n){auto* b=static_cast<uint8_t*>(p);while(n--)*b++=static_cast<uint8_t>(esp_random());}
