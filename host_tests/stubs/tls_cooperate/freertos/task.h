#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle(void);
unsigned uxTaskPriorityGet(void *);
void vTaskPrioritySet(void *,unsigned);
void vTaskDelay(unsigned);
void test_task_yield(void);
#define taskYIELD() test_task_yield()
