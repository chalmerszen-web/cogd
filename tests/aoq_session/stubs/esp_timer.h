#pragma once
#include <cstdint>
extern int64_t test_now;
inline int64_t esp_timer_get_time(){return test_now*1000;}
