#include "agent.h"
#include "runtime.h"

void app_main(void)
{
    static agent_core_t core;
    agent_core_init(&core);
    esp_agent_run(&core);
}
