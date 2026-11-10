#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void p4_diag_init(void);
/* Task context only. Bounded copy, no allocation, no Flash I/O, no waiting. */
void p4_diag_record(unsigned kind,const char *text,size_t size);
/* Returns -1 for commands outside this namespace, otherwise an agent error. */
int p4_diag_command(const char *line,char *out,size_t capacity);
#ifdef __cplusplus
}
#endif
