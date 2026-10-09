#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H
int xTaskCreate(void (*function)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelay(unsigned);
void vTaskDelete(void *);
unsigned uxTaskGetStackHighWaterMark(void *);
#endif
