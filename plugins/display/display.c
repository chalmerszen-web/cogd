#include "display.h"
#include "json.h"
#include <stdio.h>
#include <string.h>

agent_err_t agent_display_parse(const char *text,agent_display_config_t *out)
{
    if(!text || !out || strlen(text)>AGENT_ARGS_MAX) return AGENT_ERR_ARGUMENT;
    cJSON *r=agent_json_parse(text,strlen(text)); if(!r) return AGENT_ERR_JSON;
    agent_err_t error=AGENT_ERR_ARGUMENT;
    agent_display_config_t v={.utc_offset=480,.foreground=65535};
    const char *mode=agent_json_string(r,"mode");
    if(!mode || !cJSON_IsObject(r)) goto done;
    if(!strcmp(mode,"clock")) v.mode=AGENT_DISPLAY_CLOCK;
    else if(!strcmp(mode,"fill")) v.mode=AGENT_DISPLAY_FILL;
    else if(strcmp(mode,"off")) goto done;
    for(const cJSON *i=r->child;i;i=i->next) {
        if(!strcmp(i->string,"mode")) continue;
        if(v.mode==AGENT_DISPLAY_OFF) goto done;
        if(!strcmp(i->string,"utc_offset_minutes")) {
            if(v.mode!=AGENT_DISPLAY_CLOCK || !cJSON_IsNumber(i) ||
               i->valuedouble< -720 || i->valuedouble>840 || i->valuedouble!=(int)i->valuedouble) goto done;
            v.utc_offset=(int)i->valuedouble;
        } else {
            uint64_t value;
            if(!agent_json_uint(i,65535,&value)) goto done;
            if(!strcmp(i->string,"foreground")) v.foreground=(uint16_t)value;
            else if(!strcmp(i->string,"background") && v.mode==AGENT_DISPLAY_CLOCK) v.background=(uint16_t)value;
            else goto done;
        }
    }
    if(v.mode==AGENT_DISPLAY_CLOCK && v.foreground==v.background) goto done;
    *out=v;error=AGENT_OK;
done:
    cJSON_Delete(r);return error;
}

bool agent_display_time(int64_t epoch,int offset,char hms[9],char zone[10])
{
    if(!hms || !zone || offset< -720 || offset>840) return false;
    unsigned magnitude=(unsigned)(offset<0?-offset:offset);
    snprintf(zone,10,"UTC%c%02u:%02u",offset<0?'-':'+',magnitude/60,magnitude%60);
    if(epoch<INT64_C(1735689600)) { memcpy(hms,"--:--:--",9);return false; }
    /* Modulo first also avoids overflow for extreme host test epochs. */
    int64_t seconds=(epoch%86400+(int64_t)offset*60+86400)%86400;
    unsigned value=(unsigned)seconds;
    snprintf(hms,9,"%02u:%02u:%02u",value/3600,(value/60)%60,value%60);
    return true;
}

/* Original compact 5x7 glyphs, columns from left to right, low bit at top. */
static const char alphabet[]="0123456789:+-UTC";
static const uint8_t glyphs[][5]={
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46},{0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    {0,0x36,0x36,0,0},{0x08,0x08,0x3e,0x08,0x08},{0x08,0x08,0x08,0x08,0x08},
    {0x3f,0x40,0x40,0x40,0x3f},{0x01,0x01,0x7f,0x01,0x01},{0x3e,0x41,0x41,0x41,0x22}
};
static bool ink(const char *text,unsigned count,unsigned scale,unsigned x,unsigned y)
{
    unsigned cell=x/(6*scale),column=(x/scale)%6;
    if(cell>=count || column>=5 || y>=7*scale) return false;
    const char *g=strchr(alphabet,text[cell]);
    return g && *g && ((glyphs[g-alphabet][column]>>(y/scale))&1u);
}
void agent_display_row(const agent_display_config_t *c,const char hms[9],const char zone[10],unsigned y,uint8_t row[320])
{
    for(unsigned x=0;x<AGENT_DISPLAY_WIDTH;++x) {
        bool on=c->mode==AGENT_DISPLAY_FILL;
        if(c->mode==AGENT_DISPLAY_CLOCK && y<AGENT_DISPLAY_HEIGHT) {
            if(x>=8 && y>=32) on=ink(hms,8,3,x-8,y-32);
            if(x>=53 && y>=12 && y<19) on=ink(zone,9,1,x-53,y-12);
        }
        uint16_t rgb=on?c->foreground:c->background;
        row[2*x]=(uint8_t)(rgb>>8);row[2*x+1]=(uint8_t)rgb;
    }
}
