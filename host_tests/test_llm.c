#include "llm.h"
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static agent_messages_t messages;
    static agent_llm_reply_t reply;
    static char wire[AGENT_REQUEST_MAX + 1];
    assert(agent_messages_init(&messages, "Small agent") == AGENT_OK);
    assert(agent_messages_add(&messages, "user", "你好\n\"red\"", NULL) == AGENT_OK);
    size_t size;
    assert(agent_deepseek_request(&messages, false, NULL, wire, sizeof(wire), &size) == AGENT_OK);
    cJSON *root = agent_json_parse(wire, size);
    assert(root && !strcmp(agent_json_string(root, "model"), "deepseek-flash"));
    assert(!strcmp(agent_json_string(cJSON_GetObjectItemCaseSensitive(root, "thinking"), "type"), "disabled"));
    cJSON_Delete(root);
    const char *response = "{\"choices\":[{\"message\":{\"content\":\"Hello 世界\"},\"finish_reason\":\"stop\"}]}";
    agent_llm_reply_init(&reply);
    assert(agent_llm_parse(&reply, response, strlen(response), false, NULL, NULL) == AGENT_OK);
    assert(reply.done && !strcmp(reply.text, "Hello 世界"));
    assert(agent_messages_assistant(&messages, &reply) == AGENT_OK);
    root = agent_json_parse(messages.data, messages.used);
    assert(root && cJSON_GetArraySize(root) == 3); cJSON_Delete(root);
    const char *tool = "{\"choices\":[{\"message\":{\"content\":null,\"tool_calls\":[{\"id\":\"abc\",\"function\":{\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":255,\\\"g\\\":0,\\\"b\\\":0}\"}}]},\"finish_reason\":\"tool_calls\"}]}";
    agent_llm_reply_init(&reply);
    assert(agent_llm_parse(&reply, tool, strlen(tool), false, NULL, NULL) == AGENT_OK);
    assert(reply.call_count == 1 && !strcmp(agent_call_arguments(&reply, 0), "{\"r\":255,\"g\":0,\"b\":0}"));
    assert(agent_messages_assistant(&messages, &reply) == AGENT_OK);
    assert(agent_messages_add(&messages, "tool", "{\"ok\":true}", "abc") == AGENT_OK);
    assert(strstr(messages.data, "\"tool_call_id\":\"abc\""));
    /* Combined argument budget applies across independently interleaved calls. */
    agent_llm_reply_init(&reply);
    char args[2049]; memset(args,'x',2048); args[2048]=0;
    for(unsigned i=0;i<2;++i) {
        snprintf(wire,sizeof(wire),"{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":%u,\"id\":\"c%u\",\"function\":{\"name\":\"device_status_get\",\"arguments\":\"%s\"}}]}}]}",i,i,args);
        assert(!agent_llm_parse(&reply,wire,strlen(wire),true,NULL,NULL));
    }
    const char *extra="{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"x\"}}]}}]}";
    assert(agent_llm_parse(&reply,extra,strlen(extra),true,NULL,NULL)==AGENT_ERR_LIMIT);
    agent_llm_reply_init(&reply);
    const char *wrong_type="{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":true}]}}]}";
    assert(agent_llm_parse(&reply,wrong_type,strlen(wrong_type),true,NULL,NULL)==AGENT_ERR_PROTOCOL);
    agent_llm_reply_init(&reply);
    static char answer[AGENT_ANSWER_MAX+1]; memset(answer,'a',AGENT_ANSWER_MAX); answer[AGENT_ANSWER_MAX]=0;
    snprintf(wire,sizeof(wire),"{\"choices\":[{\"delta\":{\"content\":\"%s\"}}]}",answer);
    assert(!agent_llm_parse(&reply,wire,strlen(wire),true,NULL,NULL));
    const char *answer_extra="{\"choices\":[{\"delta\":{\"content\":\"a\"}}]}";
    assert(agent_llm_parse(&reply,answer_extra,strlen(answer_extra),true,NULL,NULL)==AGENT_ERR_LIMIT);
    puts("llm: requests, thinking policy, non-stream responses and continuation IDs PASS");
    return 0;
}
