#include "json.h"
#include "llm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { char data[32768];size_t used,calls,fail_at,max; } sink_t;
static sink_t streamed,failed;
static agent_err_t write_part(void *ctx,const char *data,size_t n)
{
    sink_t *s=ctx;++s->calls;
    if(s->calls==s->fail_at)return AGENT_ERR_NETWORK;
    assert(n<=4096 && n<sizeof(s->data)-s->used);
    memcpy(s->data+s->used,data,n);s->used+=n;s->data[s->used]=0;
    if(n>s->max)s->max=n;
    return AGENT_OK;
}
static void stream_checks(void)
{
    static char input[8193],stored[32768];
    const char *parts[]={"a","中","粤語","🙂","\"","\\","\n","\r","\t","\x01","\x1f"};
    uint32_t seed=0x151152u;
    for(unsigned sample=0;sample<64;++sample) {
        size_t n=0,target=sample<32?sample:2048;
        while(n<target) {
            seed=1664525u*seed+1013904223u;
            const char *part=parts[seed%(sizeof(parts)/sizeof(*parts))];
            size_t width=strlen(part);memcpy(input+n,part,width);n+=width;
        }
        input[n]=0;agent_json_writer_t w;agent_json_writer_init(&w,stored,sizeof(stored));agent_json_quote(&w,input);
        memset(&streamed,0,sizeof(streamed));assert(!agent_json_write_quote(input,write_part,&streamed));
        assert(!w.error && w.used==streamed.used && !strcmp(stored,streamed.data));
        cJSON *value=agent_json_parse(streamed.data,streamed.used);assert(cJSON_IsString(value));
        assert(!strcmp(value->valuestring,input));cJSON_Delete(value);
    }
    for(unsigned ch=1;ch<32;++ch) {
        char text[2]={(char)ch,0},gold[10];snprintf(gold,sizeof(gold),"\"\\u%04x\"",ch);
        memset(&streamed,0,sizeof(streamed));assert(!agent_json_write_quote(text,write_part,&streamed));
        assert(!strcmp(streamed.data,gold));
    }
    memset(input,'x',8190);memcpy(input+4095,"中",3);input[8190]=0;
    memset(&streamed,0,sizeof(streamed));assert(!agent_json_write_quote(input,write_part,&streamed));
    assert(streamed.max==4096 && streamed.used==8192);
    cJSON *value=agent_json_parse(streamed.data,streamed.used);assert(value && !strcmp(value->valuestring,input));cJSON_Delete(value);
    const char *text="quote\"\\\n🙂";
    memset(&streamed,0,sizeof(streamed));assert(!agent_json_write_quote(text,write_part,&streamed));
    for(size_t fail=1;fail<=streamed.calls;++fail) {
        memset(&failed,0,sizeof(failed));failed.fail_at=fail;
        assert(agent_json_write_quote(text,write_part,&failed)==AGENT_ERR_NETWORK && failed.calls==fail);
        assert(failed.used<streamed.used && !memcmp(failed.data,streamed.data,failed.used));
    }
    memset(&failed,0,sizeof(failed));
    assert(agent_json_write_quote("\xf0\x9f",write_part,&failed)==AGENT_ERR_JSON && !failed.calls);
    assert(agent_json_write_quote(NULL,write_part,&failed)==AGENT_ERR_JSON && !failed.calls);
    assert(agent_json_write_quote("valid",NULL,NULL)==AGENT_ERR_ARGUMENT);
    memset(&streamed,0,sizeof(streamed));
    assert(!agent_message_write("tool","完成\n","original-call-id",write_part,&streamed));
    value=agent_json_parse(streamed.data,streamed.used);assert(value);
    assert(!strcmp(agent_json_string(value,"tool_call_id"),"original-call-id"));
    assert(!strcmp(agent_json_string(value,"content"),"完成\n"));cJSON_Delete(value);
    puts("streamed JSON: fixed-seed UTF8, all controls, 4096-byte spans, sink errors and original call IDs PASS");
}

int main(void)
{
    stream_checks();
    const char *text = "\"hello\"\\\r\n\t中文🙂";
    char output[256];
    agent_json_writer_t w;
    agent_json_writer_init(&w, output, sizeof(output));
    agent_json_raw(&w, "{\"text\":"); agent_json_quote(&w, text); agent_json_raw(&w, "}");
    assert(w.error == AGENT_OK);
    cJSON *json = agent_json_parse(output, w.used);
    assert(json && !strcmp(agent_json_string(json, "text"), text)); cJSON_Delete(json);
    const char *bad[] = {"{", "{\"a\":1,\"a\":2}", "{\"a\":01}", "{\"a\":1.}",
        "{\"a\":1e}", "{\"a\":1e999}", "{\"x\":\"\\u0000\"}", "{\"x\":\"\n\"}",
        "{} trailing", "[1,]", "{\"x\":\"\\ud800\"}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        json = agent_json_parse(bad[i], strlen(bad[i]));
        if (json) fprintf(stderr, "incorrectly accepted: %s\n", bad[i]);
        assert(!json);
    }
    assert(!agent_utf8_valid("\xc0\x80", 2));
    assert(!agent_utf8_valid("\xf0\x9f", 2));
    assert(!agent_utf8_valid("\xed\xa0\x80", 3));
    char deep[40]; memset(deep, '[', 20); memset(deep + 20, ']', 20);
    assert(!agent_json_parse(deep, sizeof(deep)));
    agent_json_writer_init(&w, output, 3); agent_json_quote(&w, "abcd");
    assert(w.error == AGENT_ERR_LIMIT);
    puts("json: escaping, UTF-8, strict numbers, duplicates, depth and size bounds PASS");
    return 0;
}
