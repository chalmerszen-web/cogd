#pragma once
#include <mutex>
#include <cstdint>
using SemaphoreHandle_t=std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::mutex;}
inline int xSemaphoreTake(SemaphoreHandle_t m,uint32_t wait){if(!wait)return m->try_lock()?1:0;m->lock();return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t m){m->unlock();}
