#pragma once
#include <cstdint>
#include <chrono>
#include <thread>
extern int64_t test_now;
// The optional host receiver uses real sleeping without racing the main
// harness's synthetic clock. Other test tasks retain their scripted clock.
inline thread_local bool test_receiver_thread=false;
inline void vTaskDelay(unsigned ms){
 if(test_receiver_thread)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 else {test_now+=ms;std::this_thread::yield();}
}
int xTaskCreatePinnedToCore(void (*entry)(void*),const char*,unsigned,void*,unsigned,void*,int);
void vTaskDelete(void*);
