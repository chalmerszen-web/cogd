#pragma once
#include "esp_heap_caps.h"
#include <memory>
#include <new>
namespace aoq {
// Large per-operation state belongs in PSRAM, never on agent_net's bounded
// internal-RAM stack. Destruct members first, then wipe and free the object.
template<class T>struct ObjectDelete {
    void operator()(T* value)const{
        if(!value)return;
        value->~T();auto* p=reinterpret_cast<volatile unsigned char*>(value);
        for(size_t i=0;i<sizeof(T);++i)p[i]=0;
        heap_caps_free(value);
    }
};
template<class T>std::unique_ptr<T,ObjectDelete<T>> object_in_psram(){
    void* p=heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return std::unique_ptr<T,ObjectDelete<T>>(p?new(p) T():nullptr);
}
}
