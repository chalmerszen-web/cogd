unsigned uxTaskPriorityGet(void *);
void vTaskPrioritySet(void *,unsigned);
void test_task_yield(void);
#define taskYIELD() test_task_yield()
