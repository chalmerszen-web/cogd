#include "ten_memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { ALLOCATIONS=16, BYTE_LIMIT=49152 };
static struct { void *pointer; size_t size; bool heap; } blocks[ALLOCATIONS];
static size_t used;
static unsigned char *arena;
static size_t arena_size,arena_used,heap_used;
static unsigned count;
static bool accepting,failed;
#ifdef AGENT_TEN_TEST
static unsigned allowance=UINT32_MAX,calls;
void agent_ten_memory_fail_after(unsigned n) { allowance=n; calls=0; }
unsigned agent_ten_memory_calls(void) { return calls; }
#endif

bool agent_ten_memory_begin(void)
{ return agent_ten_memory_begin_at(NULL,0); }
bool agent_ten_memory_begin_at(void *memory,size_t size)
{
    if(accepting || count || (!memory && size)) return false;
    arena=NULL; arena_size=arena_used=heap_used=0;
    if(memory && size) {
        size_t alignment=_Alignof(max_align_t);
        size_t padding=(alignment-(uintptr_t)memory%alignment)%alignment;
        if(size>padding) { arena=(unsigned char *)memory+padding; arena_size=size-padding; }
    }
    failed=false; accepting=true; return true;
}
bool agent_ten_memory_seal(void) { accepting=false; return !failed; }
bool agent_ten_memory_failed(void) { return failed; }
size_t agent_ten_memory_bytes(void) { return used; }
size_t agent_ten_memory_heap_bytes(void) { return heap_used; }
size_t agent_ten_memory_arena_bytes(void) { return arena_used; }
void *agent_ten_malloc(size_t size)
{
    if(!accepting || !size || count==ALLOCATIONS || size>BYTE_LIMIT-used) {
        failed=true; return NULL;
    }
#ifdef AGENT_TEN_TEST
    if(calls++>=allowance) { failed=true; return NULL; }
#endif
    size_t alignment=_Alignof(max_align_t);
    size_t at=arena_used+(alignment-arena_used%alignment)%alignment;
    bool heap=!arena || at>arena_size || size>arena_size-at;
    void *p=heap?malloc(size):arena+at;
    if(!p) { failed=true; return NULL; }
    if(heap) heap_used+=size; else arena_used=at+size;
    blocks[count].pointer=p; blocks[count].size=size; blocks[count++].heap=heap;
    used+=size; return p;
}
void *agent_ten_calloc(size_t count,size_t size)
{
    if(size && count>SIZE_MAX/size) { failed=true; return NULL; }
    void *p=agent_ten_malloc(count*size);
    if(p) memset(p,0,count*size);
    return p;
}
void agent_ten_free(void *p)
{
    if(!p) return;
    for(unsigned i=0;i<count;++i) if(blocks[i].pointer==p) {
        used-=blocks[i].size;
        if(blocks[i].heap) { heap_used-=blocks[i].size; free(p); }
        blocks[i]=blocks[--count]; return;
    }
    failed=true;
}
void agent_ten_memory_close(void)
{
    /* Upstream loses partial constructors on failure. The bounded allocation
     * group owns those blocks even when no complete handle was returned. */
    while(count) { --count; if(blocks[count].heap) free(blocks[count].pointer); }
    used=arena_used=arena_size=heap_used=0; arena=NULL; accepting=false;
}
