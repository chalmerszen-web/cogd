#include "voice_heap.h"
int main(void)
{
    esp_agent_voice_heap_begin();
    esp_agent_voice_heap_mark(VOICE_HEAP_KWS_ALLOC_BEGIN);
    esp_agent_voice_heap_end(0);
    if(esp_agent_voice_heap_session_begin() || esp_agent_voice_heap_session_end(0))return 1;
    return 0;
}
