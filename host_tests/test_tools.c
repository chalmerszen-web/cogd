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

static agent_llm_reply_t voice_reply,original_reply;
static char hint_scratch[AGENT_ARGS_MAX+AGENT_TOOLS_MAX+5];
static void voice_call(unsigned i,const char *args)
{
    agent_tool_call_t *call=&voice_reply.calls[i];
    snprintf(call->id,sizeof(call->id),"voice-%u",i);
    strcpy(call->name,"device_light_get");call->present=true;
    call->offset=voice_reply.args_used;call->length=strlen(args);
    size_t bytes=strlen(args)+1;
    assert(bytes<=sizeof(voice_reply.arguments)-voice_reply.args_used);
    memcpy(voice_reply.arguments+voice_reply.args_used,args,bytes);
    voice_reply.args_used+=bytes;voice_reply.call_count=i+1;voice_reply.done=true;
}
static void voice_metadata(void)
{
    char normal[24576],voice[24576];agent_json_writer_t writer;
    agent_json_writer_init(&writer,normal,sizeof(normal));
    assert(!agent_tools_write(NULL,agent_json_write_bytes,&writer));
    agent_json_writer_init(&writer,voice,sizeof(voice));
    assert(!agent_tools_voice_write(NULL,agent_json_write_bytes,&writer));
    /* Whole tool definitions exceed the caller-argument JSON guard budget. */
    cJSON *plain=cJSON_Parse(normal);
    cJSON *enhanced=cJSON_Parse(voice);
    assert(plain && enhanced && cJSON_GetArraySize(enhanced)==AGENT_TOOL_COUNT);
    for(unsigned i=0;i<AGENT_TOOL_COUNT;++i) {
        cJSON *function=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(enhanced,(int)i),"function");
        cJSON *parameters=cJSON_GetObjectItemCaseSensitive(function,"parameters");
        cJSON *properties=cJSON_GetObjectItemCaseSensitive(parameters,"properties");
        cJSON *hint=cJSON_GetObjectItemCaseSensitive(properties,"final_batch");
        assert(hint && !strcmp(agent_json_string(hint,"type"),"boolean"));
        cJSON_DeleteItemFromObjectCaseSensitive(properties,"final_batch");
    }
    /* Aside from the optional top-level hint, nested schemas and requirements
     * must be byte-for-byte equivalent as JSON values. */
    assert(cJSON_Compare(plain,enhanced,true));cJSON_Delete(plain);cJSON_Delete(enhanced);
    const char *cases[]={"{}","{\"final_batch\":false}","{\"final_batch\":true}",
        "{\"final\\u005fbatch\":true}","{\"final_batch\":1}",
        "{\"final_batch\":true,\"final_batch\":false}"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        memset(&voice_reply,0,sizeof(voice_reply));voice_call(0,cases[i]);
        original_reply=voice_reply;bool final=false;
        agent_err_t error=agent_tools_voice_hint(&voice_reply,hint_scratch,sizeof(hint_scratch),&final);
        if(i>=4)assert(error && !memcmp(&voice_reply,&original_reply,sizeof(voice_reply)));
        else assert(!error && final==(i==2 || i==3) &&
                    !strcmp(agent_call_arguments(&voice_reply,0),"{}") && !agent_tools_validate(&voice_reply));
    }
    for(unsigned bad=0;bad<2;++bad) {
        memset(&voice_reply,0,sizeof(voice_reply));
        voice_call(0,bad?"{\"final_batch\":true}":"{\"final_batch\":false}");
        voice_call(1,bad?"{}":"{\"final_batch\":\"true\"}");
        original_reply=voice_reply;bool final=false;
        assert(agent_tools_voice_hint(&voice_reply,hint_scratch,sizeof(hint_scratch),&final)==AGENT_ERR_ARGUMENT);
        assert(!final && !memcmp(&voice_reply,&original_reply,sizeof(voice_reply)));
    }
    memset(&voice_reply,0,sizeof(voice_reply));
    for(unsigned i=0;i<AGENT_TOOLS_MAX;++i)
        voice_call(i,i+1==AGENT_TOOLS_MAX?"{\"final_batch\":true}":"{\"final_batch\":false}");
    bool final=false;
    assert(!agent_tools_voice_hint(&voice_reply,hint_scratch,sizeof(hint_scratch),&final));
    assert(final && voice_reply.call_count==AGENT_TOOLS_MAX && !agent_tools_validate(&voice_reply));
    for(unsigned i=0;i<AGENT_TOOLS_MAX;++i)assert(!strcmp(agent_call_arguments(&voice_reply,i),"{}") && voice_reply.calls[i].length==2);
    original_reply=voice_reply;
    assert(agent_tools_voice_hint(&voice_reply,hint_scratch,sizeof(hint_scratch)-1,&final)==AGENT_ERR_ARGUMENT);
    assert(!memcmp(&voice_reply,&original_reply,sizeof(voice_reply)));
    voice_reply.calls[3].offset=sizeof(voice_reply.arguments);
    original_reply=voice_reply;
    assert(agent_tools_voice_hint(&voice_reply,hint_scratch,sizeof(hint_scratch),&final)==AGENT_ERR_PROTOCOL);
    assert(!memcmp(&voice_reply,&original_reply,sizeof(voice_reply)));
}

static void summary_feedback(void)
{
    char text[1540],args[1700],detail[220];
    for(unsigned i=0;i<1539;i+=3)memcpy(text+i,"中",3);
    text[1539]=0;snprintf(args,sizeof(args),"{\"text\":\"%s\"}",text);
    assert(agent_tool_check("agent_context_summary_set",args,detail,sizeof(detail))==AGENT_ERR_ARGUMENT);
    assert(strstr(detail,"1539 UTF-8 bytes") && strstr(detail,"limit 1536"));
    for(size_t cap=0;cap<40;++cap) {
        char guarded[42];memset(guarded,0x5a,sizeof(guarded));
        assert(agent_tool_check("agent_context_summary_set",args,guarded+1,cap)==AGENT_ERR_ARGUMENT);
        assert(guarded[0]==0x5a && guarded[cap+1]==0x5a);
        if(cap)assert(memchr(guarded+1,0,cap));
    }
    assert(agent_tool_check("agent_context_summary_set",args,NULL,0)==AGENT_ERR_ARGUMENT);
    text[1536]=0;snprintf(args,sizeof(args),"{\"text\":\"%s\",\"through_seq\":0}",text);
    assert(!agent_tool_check("agent_context_summary_set",args,detail,sizeof(detail)));
    const char *invalid[]={"{\"text\":\"x\",\"through_seq\":true}",
        "{\"text\":\"x\",\"through_seq\":\"device:123\"}",
        "{\"text\":\"x\",\"through_seq\":-1}","{\"text\":\"x\",\"unexpected\":0}",
        "{\"text\":\"\"}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        assert(agent_tool_check("agent_context_summary_set",invalid[i],detail,sizeof(detail))==AGENT_ERR_ARGUMENT);
        assert(strstr(detail,"through_seq") && strstr(detail,"UTF-8 bytes"));
    }
}

int main(void)
{
    summary_feedback();
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
    assert(agent_tool_invoke(&ops,"device_light_get","{\"final_batch\":true}",output,sizeof(output))==AGENT_ERR_ARGUMENT);
    voice_metadata();
    puts("tools: allowlist, schema, ranges, extra/duplicate keys and no side effect on invalid input PASS");
    return 0;
}
