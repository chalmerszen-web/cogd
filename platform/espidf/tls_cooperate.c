#include "tls_cooperate.h"
#include "runtime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>

static _Atomic(TaskHandle_t) owner;
static atomic_uint steps, max_step_ms;
static unsigned step_at; /* Written only by the scoped task. */

bool esp_agent_tls_cooperate_current(void)
{
    TaskHandle_t task = atomic_load_explicit(&owner, memory_order_acquire);
    return task && task == xTaskGetCurrentTaskHandle();
}
bool esp_agent_tls_cooperate_begin(void)
{
    TaskHandle_t expected = NULL;
    if (!atomic_compare_exchange_strong_explicit(&owner, &expected,
            xTaskGetCurrentTaskHandle(), memory_order_acq_rel, memory_order_acquire)) return false;
    atomic_store(&steps, 0);
    atomic_store(&max_step_ms, 0);
    step_at = 0;
    return true;
}
void esp_agent_tls_cooperate_end(void)
{
    TaskHandle_t expected = xTaskGetCurrentTaskHandle();
    (void)atomic_compare_exchange_strong_explicit(&owner, &expected, NULL,
        memory_order_release, memory_order_relaxed);
}
void agent_tls_cooperate_step(int finished)
{
    if (!esp_agent_tls_cooperate_current()) return;
    unsigned now = (unsigned)esp_agent_now();
    if (!finished) { step_at = now; return; }
    unsigned elapsed = now - step_at;
    if (elapsed > atomic_load(&max_step_ms)) atomic_store(&max_step_ms, elapsed);
    atomic_fetch_add(&steps, 1);
    /* Yield to lower priorities as well. No WDT reset or altered TLS result. */
    vTaskDelay(1);
}
void esp_agent_tls_cooperate_stats(unsigned *count, unsigned *maximum)
{
    *count = atomic_load(&steps);
    *maximum = atomic_load(&max_step_ms);
}
