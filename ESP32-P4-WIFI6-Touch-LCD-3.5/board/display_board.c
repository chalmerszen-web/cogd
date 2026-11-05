#include "display_board.h"
#include "p4_agent.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
static agent_display_config_t config={.mode=AGENT_DISPLAY_CLOCK,.utc_offset=480,.foreground=65535};
static lv_obj_t *clock_text,*status_text;
static int64_t last_second=-1;
static portMUX_TYPE config_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned generation=1;
static atomic_uint frames,applied_generation;
static agent_display_config_t snapshot(unsigned *g)
{
    portENTER_CRITICAL(&config_lock);
    agent_display_config_t c=config;if(g)*g=generation;
    portEXIT_CRITICAL(&config_lock);return c;
}
static agent_err_t set(void *p,const agent_display_config_t *c)
{
    (void)p;if(!c || c->mode>AGENT_DISPLAY_FILL)return AGENT_ERR_ARGUMENT;
    p4_visual_stop();
    portENTER_CRITICAL(&config_lock);config=*c;++generation;portEXIT_CRITICAL(&config_lock);
    return AGENT_OK;
}
static agent_err_t status(void *p,char *out,size_t cap)
{
    (void)p;unsigned g;agent_display_config_t c=snapshot(&g);
    int n=snprintf(out,cap,"{\"ready\":%s,\"width\":320,\"height\":480,\"mode\":%u,\"visual_active\":%s,\"frames\":%u,\"applied\":%s,\"background\":%u}",
        clock_text?"true":"false",c.mode,p4_visual_active()?"true":"false",atomic_load(&frames),
        !p4_visual_active()&&atomic_load(&applied_generation)==g?"true":"false",c.background);
    return n<0 || (size_t)n>=cap?AGENT_ERR_LIMIT:AGENT_OK;
}
const agent_display_ops_t esp_hi_display_ops={set,status,NULL};
agent_err_t esp_hi_display_init(void)
{
    lv_display_t *display=bsp_display_start();if(!display)return AGENT_ERR_TOOL;
    bsp_display_backlight_on();
    if(!bsp_display_lock(1000))return AGENT_ERR_TIMEOUT;
    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x101828),0);
    clock_text=lv_label_create(screen);lv_obj_set_style_text_color(clock_text,lv_color_white(),0);
    lv_obj_set_style_text_font(clock_text,&lv_font_montserrat_48,0);
    lv_obj_align(clock_text,LV_ALIGN_CENTER,0,-35);lv_label_set_text(clock_text,"--:--:--");
    status_text=lv_label_create(screen);lv_obj_set_style_text_color(status_text,lv_color_hex(0x90c8ff),0);
    lv_obj_align(status_text,LV_ALIGN_CENTER,0,45);lv_label_set_text(status_text,"cogd / ESP32-P4\nVoice assistant");
    bsp_display_unlock();p4_visual_init();return AGENT_OK;
}
void esp_hi_display_tick(uint64_t ms)
{
    (void)ms;if(p4_visual_active())return;unsigned g;agent_display_config_t c=snapshot(&g);int64_t second=time(NULL);
    bool changed=g!=atomic_load(&applied_generation);
    if(!clock_text || (!changed && (c.mode!=AGENT_DISPLAY_CLOCK || second==last_second)))return;
    if(!bsp_display_lock(100))return;
    char hms[9],zone[10];agent_display_time(second,c.utc_offset,hms,zone);
    lv_label_set_text(clock_text,c.mode==AGENT_DISPLAY_CLOCK?hms:"");
    lv_label_set_text(status_text,c.mode==AGENT_DISPLAY_CLOCK?"cogd / ESP32-P4\nVoice assistant":"");
    uint16_t rgb=c.mode==AGENT_DISPLAY_FILL?c.foreground:c.background;
    lv_color_t bg=lv_color_make((uint8_t)(((rgb>>11)&31)*255/31),(uint8_t)(((rgb>>5)&63)*255/63),(uint8_t)((rgb&31)*255/31));
    rgb=c.foreground;
    lv_color_t fg=lv_color_make((uint8_t)(((rgb>>11)&31)*255/31),(uint8_t)(((rgb>>5)&63)*255/63),(uint8_t)((rgb&31)*255/31));
    lv_obj_set_style_bg_color(lv_screen_active(),c.mode==AGENT_DISPLAY_OFF?lv_color_black():bg,0);
    lv_obj_set_style_text_color(clock_text,fg,0);
    if(changed)bsp_display_brightness_set(c.mode==AGENT_DISPLAY_OFF?0:100);
    last_second=second;atomic_fetch_add(&frames,1);atomic_store(&applied_generation,g);bsp_display_unlock();
}
void esp_hi_display_cancel(void) {agent_display_config_t c=snapshot(NULL);c.mode=AGENT_DISPLAY_OFF;set(NULL,&c);}
