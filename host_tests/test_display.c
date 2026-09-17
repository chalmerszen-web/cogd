#include "display.h"
#include "tools.h"
#include "json.h"
#include "resources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned writes;
static agent_err_t set(void *ctx,const agent_display_config_t *c)
{ (void)ctx;assert(c->mode==AGENT_DISPLAY_CLOCK);++writes;return AGENT_OK; }
static agent_err_t status(void *ctx,char *out,size_t cap)
{ (void)ctx;snprintf(out,cap,"{\"pending\":true}");return AGENT_OK; }
int main(void)
{
    agent_display_config_t c={0};
    assert(!agent_display_parse("{\"mode\":\"clock\"}",&c));
    assert(c.mode==AGENT_DISPLAY_CLOCK && c.utc_offset==480 && c.foreground==65535 && !c.background);
    assert(!agent_display_parse("{\"mode\":\"fill\",\"foreground\":63488}",&c) && c.foreground==63488);
    assert(!agent_display_parse("{\"mode\":\"off\"}",&c) && c.mode==AGENT_DISPLAY_OFF);
    const char *bad[]={"{}","[]","{\"mode\":\"clock\",\"mode\":\"off\"}",
        "{\"mode\":\"clock\",\"utc_offset_minutes\":841}","{\"mode\":\"clock\",\"utc_offset_minutes\":-721}",
        "{\"mode\":\"clock\",\"utc_offset_minutes\":1.5}","{\"mode\":\"clock\",\"utc_offset_minutes\":true}",
        "{\"mode\":\"clock\",\"foreground\":0}","{\"mode\":\"clock\",\"foreground\":65536}",
        "{\"mode\":\"off\",\"foreground\":1}","{\"mode\":\"fill\",\"background\":1}",
        "{\"mode\":\"fill\",\"utc_offset_minutes\":0}","{\"mode\":\"clock\",\"pin\":19}"};
    const agent_display_ops_t display={set,status,NULL};
    const agent_tool_ops_t tools={.display=&display};char output[128];
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        agent_display_config_t before=c;
        assert(agent_display_parse(bad[i],&c) && !memcmp(&c,&before,sizeof(c)));
        assert(agent_tool_invoke(&tools,"device_display_set",bad[i],output,sizeof(output)));
    }
    assert(!writes);
    assert(!agent_tool_invoke(&tools,"device.display.set","{\"mode\":\"clock\"}",output,sizeof(output)) && writes==1);
    assert(!strcmp(output,"{\"pending\":true}"));
    assert(!agent_tool_invoke(&tools,"device_display_get","{}",output,sizeof(output)));
    char hms[9],zone[10];
    assert(agent_display_time(1735689600,480,hms,zone) && !strcmp(hms,"08:00:00") && !strcmp(zone,"UTC+08:00"));
    assert(agent_display_time(1735689600,-720,hms,zone) && !strcmp(hms,"12:00:00") && !strcmp(zone,"UTC-12:00"));
    assert(agent_display_time(1735775999,345,hms,zone) && !strcmp(hms,"05:44:59") && !strcmp(zone,"UTC+05:45"));
    assert(agent_display_time(INT64_MAX,840,hms,zone));
    assert(!agent_display_time(0,480,hms,zone) && !strcmp(hms,"--:--:--"));
    assert(!agent_display_time(1735689600,841,hms,zone));
    struct { uint8_t before,row[320],after; } image={.before=0xa5,.after=0x5a};
    c=(agent_display_config_t){.mode=AGENT_DISPLAY_FILL,.foreground=0xf800};
    agent_display_row(&c,hms,zone,0,image.row);
    for(unsigned i=0;i<160;++i) assert(image.row[2*i]==0xf8 && image.row[2*i+1]==0);
    assert(!agent_display_parse("{\"mode\":\"clock\"}",&c));
    assert(agent_display_time(1735689600,480,hms,zone));
    unsigned ink=0;
    for(unsigned y=0;y<80;++y) {
        agent_display_row(&c,hms,zone,y,image.row);
        assert(image.before==0xa5 && image.after==0x5a);
        for(unsigned x=0;x<160;++x) {
            assert(image.row[2*x]==image.row[2*x+1]);
            if(image.row[2*x]) {assert((y>=12 && y<19) || (y>=32 && y<53));++ink;}
        }
    }
    assert(ink>600 && ink<2000);
    agent_resources_t resources;agent_resources_init(&resources);uint32_t added;
    uint32_t pins=AGENT_PIN(4)|AGENT_PIN(5)|AGENT_PIN(10);
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DISPLAY,pins,&added) && added==pins);
    assert(agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(10),&added)==AGENT_ERR_BUSY);
    assert(agent_resources_claim(&resources,AGENT_OWNER_PLAN,AGENT_PIN(4)|AGENT_PIN(8),&added)==AGENT_ERR_BUSY);
    unsigned owner;assert(!agent_resources_owner(&resources,8,&owner) && !owner);
    assert(!agent_resources_release(&resources,AGENT_OWNER_DISPLAY,pins));
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(10),&added));
    puts("display: validation, side effects, timezone, RGB565 glyph bounds and shared pin ownership PASS");
}
