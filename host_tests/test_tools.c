#include "tools.h"
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned writes;
static uint8_t state[3];
static agent_err_t set(void *ctx, const uint8_t rgb[3]) { (void)ctx; ++writes; memcpy(state, rgb, 3); return AGENT_OK; }
static agent_err_t get(void *ctx, uint8_t rgb[3]) { (void)ctx; memcpy(rgb, state, 3); return AGENT_OK; }
static uint64_t elapsed,clock_ms;
static uint64_t now(void *ctx) { (void)ctx; return clock_ms; }
static agent_err_t slow_get(void *ctx,uint8_t rgb[3]) { clock_ms+=elapsed; return get(ctx,rgb); }
static agent_err_t search(void *ctx,const char *query,uint64_t before,unsigned limit,char *out,size_t cap)
{ (void)ctx; (void)query; (void)before; (void)limit; clock_ms+=elapsed; snprintf(out,cap,"{\"hits\":[]}"); return AGENT_OK; }
static agent_err_t summary(void *ctx,const char *text,uint64_t through)
{ (void)ctx; (void)text; (void)through; clock_ms+=elapsed; return AGENT_OK; }
static agent_err_t summary_get(void *ctx,char *out,size_t cap)
{ (void)ctx; snprintf(out,cap,"{}"); return AGENT_OK; }

int main(void)
{
    agent_tool_ops_t ops = { .light_set = set, .light_get = get };
    char output[256];
    assert(agent_tool_invoke(&ops, "device_light_set_rgb", "{\"r\":255,\"g\":0,\"b\":7}", output, sizeof(output)) == AGENT_OK);
    assert(writes == 1 && state[0] == 255 && state[2] == 7);
    const char *bad[] = {"{}", "[]", "{\"r\":-1,\"g\":0,\"b\":0}", "{\"r\":256,\"g\":0,\"b\":0}",
        "{\"r\":true,\"g\":0,\"b\":0}", "{\"r\":1.5,\"g\":0,\"b\":0}",
        "{\"r\":1,\"g\":0,\"b\":0,\"pin\":19}", "{\"r\":1,\"g\":0,\"b\":0,\"r\":2}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(agent_tool_invoke(&ops, "device.light.set_rgb", bad[i], output, sizeof(output)) != AGENT_OK);
    assert(agent_tool_invoke(&ops, "shell.exec", "{}", output, sizeof(output)) == AGENT_ERR_TOOL);
    assert(writes == 1);
    assert(agent_tool_invoke(&ops, "device.light.get", "{\"extra\":1}", output, sizeof(output)) == AGENT_ERR_ARGUMENT);
    assert(agent_tool_invoke(&ops, "device.light.get", "{}", output, sizeof(output)) == AGENT_OK);
    assert(agent_tool_invoke(&ops,"agent_context_search","{\"query\":\"hello\",\"limit\":0}",output,sizeof(output))==AGENT_ERR_ARGUMENT);
    assert(agent_tool_invoke(&ops,"agent_context_search","{\"query\":\"hello\",\"typo\":1}",output,sizeof(output))==AGENT_ERR_ARGUMENT);
    assert(agent_tool_invoke(&ops,"agent_context_summary_set","{\"text\":\"facts\",\"through_seq\":true}",output,sizeof(output))==AGENT_ERR_ARGUMENT);
    assert(agent_tool_invoke(&ops,"agent_context_summary_set","{\"text\":\"\"}",output,sizeof(output))==AGENT_ERR_ARGUMENT);
    char text[16384]; agent_json_writer_t writer; agent_json_writer_init(&writer,text,sizeof(text));
    assert(!agent_tools_write(NULL,agent_json_write_bytes,&writer));
    cJSON *schema = cJSON_Parse(text);
    assert(schema && cJSON_GetArraySize(schema) == AGENT_TOOL_COUNT);
    for(unsigned i=0;i<AGENT_TOOL_COUNT;++i) {
        const cJSON *function=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(schema,(int)i),"function");
        assert(!strcmp(agent_json_string(function,"name"),agent_tool_descriptions[i].wire_name));
    }
    cJSON_Delete(schema);
    /* A full Flash search can exceed the fast GPIO budget without failing the turn. */
    ops.now_ms=now; ops.context_search=search; ops.summary_set=summary; ops.summary_get=summary_get;
    elapsed=3300;
    assert(!agent_tool_invoke(&ops,"agent_context_search","{\"query\":\"older fact\"}",output,sizeof(output)));
    assert(!agent_tool_invoke(&ops,"agent_context_summary_set","{\"text\":\"source fact\"}",output,sizeof(output)));
    ops.light_get=slow_get;
    assert(agent_tool_invoke(&ops,"device_light_get","{}",output,sizeof(output))==AGENT_ERR_TIMEOUT);
    elapsed=10001;
    assert(agent_tool_invoke(&ops,"agent_context_search","{\"query\":\"older fact\"}",output,sizeof(output))==AGENT_ERR_TIMEOUT);
    assert(agent_tool_invoke(&ops,"agent_context_summary_set","{\"text\":\"source fact\"}",output,sizeof(output))==AGENT_ERR_TIMEOUT);
    puts("tools: allowlist, schema, ranges, extra/duplicate keys and no side effect on invalid input PASS");
    return 0;
}
