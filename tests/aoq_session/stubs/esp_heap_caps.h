#pragma once
#include <cstdlib>
#include <cassert>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n,unsigned flags){assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return std::malloc(n);}
inline void* heap_caps_calloc(size_t n,size_t m,unsigned flags){assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return std::calloc(n,m);}
inline void heap_caps_free(void* p){std::free(p);}
