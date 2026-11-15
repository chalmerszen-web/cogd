#include "tools.h"
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks,calls;
static const char *expected;
static int rejected;
int p4_agent_check(const char *name,const char *args)
{ assert(!strcmp(name,expected) && !strcmp(args,"{}"));++checks;return rejected; }
int p4_agent_call(const char *name,const char *args,char *out,size_t cap)
{ assert(!strcmp(name,expected) && !strcmp(args,"{}"));++calls;snprintf(out,cap,"{\"board_result\":true}");return 0; }
int main(void)
{
    static char schemas[32768];agent_json_writer_t w;
    agent_json_writer_init(&w,schemas,sizeof(schemas));
    assert(!agent_tools_write(NULL,agent_json_write_bytes,&w));
    cJSON *list=cJSON_Parse(schemas);assert(list && cJSON_GetArraySize(list)==AGENT_TOOL_COUNT);
    for(unsigned i=0;i<AGENT_TOOL_COUNT;++i){
        cJSON *f=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(list,(int)i),"function");
        assert(!strcmp(agent_json_string(f,"name"),agent_tool_descriptions[i].wire_name));
    }
    cJSON_Delete(list);
    const char *names[]={"device_camera_test","device_display_draw","device_display_image",
        "device_web_search","qianwen_skill","device_display_video","device_tutor","device_reason"};
    agent_tool_ops_t ops={0};char out[128];
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i){
        expected=names[i];checks=calls=0;rejected=0;
        assert(!agent_tool_invoke(&ops,expected,"{}",out,sizeof(out)));
        assert(checks==1 && calls==1 && strstr(out,"board_result"));
        rejected=AGENT_ERR_ARGUMENT;
        assert(agent_tool_invoke(&ops,expected,"{}",out,sizeof(out))==AGENT_ERR_ARGUMENT);
        assert(checks==2 && calls==1);
    }
    puts("P4 schemas and all eight board dispatches, including reasoning, passed");return 0;
}
