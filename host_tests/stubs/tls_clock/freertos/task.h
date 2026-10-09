#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle(void);
typedef struct { unsigned ulRunTimeCounter; } TaskStatus_t;
typedef enum { eRunning } eTaskState;
void vTaskGetInfo(TaskHandle_t,TaskStatus_t *,int,eTaskState);
