#ifndef CANDIDATE_TEST_RTOS_H
#define CANDIDATE_TEST_RTOS_H
#include <stdint.h>
#include <assert.h>
typedef uint8_t StackType_t;
typedef struct { unsigned char reserved[256]; } StaticTask_t;
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
#define configASSERT(test) assert(test)
#endif
