#include "FreeRTOS.h"
typedef enum {eRunning,eSuspended} eTaskState;
TaskHandle_t xTaskCreateStatic(void (*)(void *),const char *,unsigned,void *,unsigned,StackType_t *,StaticTask_t *);
void vTaskDelay(unsigned);
void vTaskDelete(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
eTaskState eTaskGetState(TaskHandle_t);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
