#ifndef AGENT_TEN_MEMORY_H
#define AGENT_TEN_MEMORY_H
#include <stdbool.h>
#include <stddef.h>

/* One owned TEN instance. Creation is serialized before the worker starts;
 * close follows a joined worker. No global libc allocator is replaced. */
bool agent_ten_memory_begin(void);
bool agent_ten_memory_begin_at(void *,size_t);
bool agent_ten_memory_seal(void);
void agent_ten_memory_close(void);
size_t agent_ten_memory_bytes(void);
size_t agent_ten_memory_heap_bytes(void);
size_t agent_ten_memory_arena_bytes(void);
bool agent_ten_memory_failed(void);
void *agent_ten_malloc(size_t);
void *agent_ten_calloc(size_t,size_t);
void agent_ten_free(void *);
#ifdef AGENT_TEN_TEST
void agent_ten_memory_fail_after(unsigned);
unsigned agent_ten_memory_calls(void);
#endif
#endif
