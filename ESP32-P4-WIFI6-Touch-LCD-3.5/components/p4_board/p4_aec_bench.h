#pragma once
#include <stddef.h>
/* Explicit offline diagnostic. No microphone input or speaker output. */
int p4_aec_bench(char *out,size_t cap);
int p4_aec_tasks(char *out,size_t cap);
