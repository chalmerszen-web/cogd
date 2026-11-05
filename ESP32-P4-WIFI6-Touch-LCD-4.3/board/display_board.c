#include "display_board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include <stdio.h>
#include <time.h>
static agent_display_config_t config={.mode=AGENT_DISPLAY_CLOCK,.utc_offset=480,.foreground=65535};
static lv_obj_t *clock_text,*status_text;
static int64_t last_second=-1;
static unsigned frames;
static agent_err_t set(void *p,const agent_display_config_t *c)
{(void)p;if(!c)return AGENT_ERR_ARGUMENT;config=*c;last_second=-1;return AGENT_OK;}
static agent_err_t status(void *p,char *out,size_t cap)
{
    (void)p;int n=snprintf(out,cap,"{\"ready\":%s,\"width\":480,\"height\":800,\"mode\":%u,\"frames\":%u}",
        clock_text?"true":"false",config.mode,frames);
    return n<0 || (size_t)n>=cap?AGENT_ERR_LIMIT:AGENT_OK;
}
const agent_display_ops_t esp_hi_display_ops={set,status,NULL};
agent_err_t esp_hi_display_init(void)
{
    if(!bsp_display_start())return AGENT_ERR_TOOL;
    bsp_display_backlight_on();
    if(bsp_display_lock(1000)!=ESP_OK)return AGENT_ERR_TIMEOUT;
    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x101828),0);
    clock_text=lv_label_create(screen);lv_obj_set_style_text_color(clock_text,lv_color_white(),0);
    lv_obj_set_style_text_font(clock_text,&lv_font_montserrat_48,0);
    lv_obj_align(clock_text,LV_ALIGN_CENTER,0,-35);lv_label_set_text(clock_text,"--:--:--");
    status_text=lv_label_create(screen);lv_obj_set_style_text_color(status_text,lv_color_hex(0x90c8ff),0);
    lv_obj_align(status_text,LV_ALIGN_CENTER,0,45);lv_label_set_text(status_text,"cogd / ESP32-P4\nVoice assistant");
    bsp_display_unlock();return AGENT_OK;
}
void esp_hi_display_tick(uint64_t ms)
{
    (void)ms;int64_t second=time(NULL);if(second==last_second || !clock_text)return;
    if(bsp_display_lock(20)!=ESP_OK)return;
    char hms[9],zone[10];agent_display_time(second,config.utc_offset,hms,zone);
    lv_label_set_text(clock_text,config.mode==AGENT_DISPLAY_CLOCK?hms:"");
    lv_obj_set_style_bg_color(lv_screen_active(),config.mode==AGENT_DISPLAY_OFF?lv_color_black():lv_color_hex(0x101828),0);
    last_second=second;++frames;bsp_display_unlock();
}
void esp_hi_display_cancel(void) {config.mode=AGENT_DISPLAY_OFF;last_second=-1;}
