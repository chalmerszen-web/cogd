#include "display_board.h"
#include "control_board.h"
#include "json.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

/* Register values follow the pinned ESP-HI BSP. MIT notice: third_party/esp-hi-lcd/LICENSE. */
typedef struct { uint8_t command,length,data[16]; } panel_command_t;
static const panel_command_t panel_init[]={
    {0xb1,3,{0x05,0x3a,0x3a}},{0xb2,3,{0x05,0x3a,0x3a}},
    {0xb3,6,{0x05,0x3a,0x3a,0x05,0x3a,0x3a}},{0xb4,1,{0x03}},
    {0xc0,3,{0x44,0x04,0x04}},{0xc1,1,{0xc0}},{0xc2,2,{0x0d,0x00}},
    {0xc3,2,{0x8d,0x6a}},{0xc4,2,{0x8d,0xee}},{0xc5,1,{0x08}},
    {0xe0,16,{0x0f,0x10,0x03,0x03,0x07,0x02,0x00,0x02,0x07,0x0c,0x13,0x38,0x0a,0x0e,0x03,0x10}},
    {0xe1,16,{0x10,0x0b,0x04,0x04,0x10,0x03,0x00,0x03,0x03,0x09,0x17,0x33,0x0b,0x0c,0x06,0x10}},
    {0x35,1,{0x00}},{0x3a,1,{0x05}},
    /* BSP: mirror X=false,Y=true,swap XY=true; vendor BGR bit retained. */
    {0x36,1,{0xa8}},{0x20,0,{0}},{0x29,0,{0}}
};
enum { PANEL_OFF, PANEL_RESET, PANEL_SLEEP, PANEL_READY };
static struct {
    SemaphoreHandle_t lock;
    esp_lcd_panel_io_handle_t io;
    agent_display_config_t config;
    bool bus,claimed,failed,pending,time_valid;
    unsigned phase,row,job,frames,rendered_job,max_tick_us;
    uint64_t ready_at;
    int64_t last_second;
    agent_err_t error;
    esp_err_t sdk_error;
    char hms[9],zone[10];
    uint8_t pixels[320];
} lcd;
static StaticSemaphore_t lock_storage;
static atomic_bool cancelled;

static esp_err_t command(uint8_t value,const void *data,size_t size)
{
    return esp_lcd_panel_io_tx_param(lcd.io,value,data,size);
}
static esp_err_t start(void)
{
    spi_bus_config_t bus={.mosi_io_num=4,.miso_io_num=-1,.sclk_io_num=5,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=64};
    esp_err_t e=spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_DISABLED);if(e) return e;
    lcd.bus=true;
    esp_lcd_panel_io_spi_config_t device={.pclk_hz=8000000,.spi_mode=0,
        .cs_gpio_num=-1,.dc_gpio_num=10,.trans_queue_depth=1,.lcd_cmd_bits=8,.lcd_param_bits=8};
    e=esp_lcd_new_panel_io_spi(SPI2_HOST,&device,&lcd.io);
    if(!e) e=command(0x01,NULL,0);
    return e;
}
static agent_err_t stop(void)
{
    esp_err_t e=ESP_OK;
    if(lcd.io) {
        e=command(0x28,NULL,0);
        esp_err_t removed=esp_lcd_panel_io_del(lcd.io);
        if(removed) return AGENT_ERR_TOOL;
        lcd.io=NULL;
    }
    if(lcd.bus) {
        esp_err_t freed=spi_bus_free(SPI2_HOST);if(freed) return AGENT_ERR_TOOL;
        lcd.bus=false;
    }
    if(lcd.claimed) {
        agent_err_t released=esp_hi_display_pins(false,&lcd.claimed);if(released) return released;
    }
    lcd.phase=PANEL_OFF;lcd.pending=false;
    if(e) {lcd.sdk_error=e;return AGENT_ERR_TOOL;}
    return AGENT_OK;
}
static agent_err_t set(void *ctx,const agent_display_config_t *config)
{
    (void)ctx;
    if(!config || config->mode>AGENT_DISPLAY_FILL || config->mode<AGENT_DISPLAY_OFF ||
       config->utc_offset< -720 || config->utc_offset>840 ||
       (config->mode==AGENT_DISPLAY_CLOCK && config->foreground==config->background)) return AGENT_ERR_ARGUMENT;
    if(!lcd.lock) return AGENT_ERR_CONFIG;
    if(xSemaphoreTake(lcd.lock,pdMS_TO_TICKS(30))!=pdTRUE) return AGENT_ERR_BUSY;
    agent_err_t e=AGENT_OK;
    if(lcd.failed && (lcd.bus || lcd.claimed)) {xSemaphoreGive(lcd.lock);return AGENT_ERR_BUSY;}
    if(config->mode!=AGENT_DISPLAY_OFF && !lcd.claimed) {
        e=esp_hi_display_pins(true,&lcd.claimed);
        if(e && lcd.claimed) {lcd.failed=true;lcd.error=e;}
    }
    if(!e) {
        lcd.config=*config;lcd.job++;lcd.pending=true;lcd.failed=false;
        lcd.error=AGENT_OK;lcd.sdk_error=ESP_OK;lcd.row=0;lcd.last_second=-1;
    }
    xSemaphoreGive(lcd.lock);return e;
}
static agent_err_t status(void *ctx,char *out,size_t cap)
{
    (void)ctx;if(!lcd.lock) return AGENT_ERR_CONFIG;
    if(xSemaphoreTake(lcd.lock,pdMS_TO_TICKS(30))!=pdTRUE) return AGENT_ERR_BUSY;
    agent_json_writer_t w;agent_json_writer_init(&w,out,cap);
    static const char *const modes[]={"off","clock","fill"};
    agent_json_printf(&w,"{\"mode\":\"%s\",\"ready\":%s,\"pending\":%s,\"pins_owned\":%s,"
        "\"job\":%u,\"rendered_job\":%u,\"frames\":%u,\"time_valid\":%s,\"time\":\"%s\","
        "\"utc_offset_minutes\":%d,\"foreground\":%u,\"background\":%u,\"max_tick_us\":%u,"
        "\"error\":\"%s\",\"sdk_error\":%d,\"width\":160,\"height\":80,\"profile\":\"esp_hi_st7735_bsp\","
        "\"transport\":\"esp_lcd_spi\",\"controller_verified\":false,\"visual_verified\":false}",modes[lcd.config.mode],
        lcd.phase==PANEL_READY&&!lcd.failed?"true":"false",lcd.pending?"true":"false",lcd.claimed?"true":"false",
        lcd.job,lcd.rendered_job,lcd.frames,lcd.time_valid?"true":"false",lcd.hms,lcd.config.utc_offset,
        lcd.config.foreground,lcd.config.background,lcd.max_tick_us,agent_err_name(lcd.error),(int)lcd.sdk_error);
    xSemaphoreGive(lcd.lock);return w.error;
}
const agent_display_ops_t esp_hi_display_ops={set,status,NULL};

agent_err_t esp_hi_display_init(void)
{
    if(!lcd.lock) {
        lcd.lock=xSemaphoreCreateMutexStatic(&lock_storage);
        lcd.config=(agent_display_config_t){.utc_offset=480,.foreground=65535};
        memcpy(lcd.hms,"--:--:--",9);
    }
    return lcd.lock?AGENT_OK:AGENT_ERR_MEMORY;
}
void esp_hi_display_cancel(void)
{
    /* Used by the always-available USB cancel path, independently of network admission. */
    atomic_store(&cancelled,true);
}
static esp_err_t row(unsigned y)
{
    uint8_t x[]={0,0,0,159},v[]={0,(uint8_t)(y+24),0,(uint8_t)(y+24)};
    agent_display_row(&lcd.config,lcd.hms,lcd.zone,y,lcd.pixels);
    esp_err_t e=command(0x2a,x,sizeof(x));
    if(!e) e=command(0x2b,v,sizeof(v));
    /* Poll small chunks: queued transfers stall when long Flash history reads
     * delay the SPI ISR. Panel IO still owns DC and half-duplex bus sequencing. */
    for(size_t offset=0;!e && offset<sizeof(lcd.pixels);offset+=64)
        e=esp_lcd_panel_io_tx_param(lcd.io,offset?-1:0x2c,lcd.pixels+offset,64);
    return e;
}
void esp_hi_display_tick(uint64_t now)
{
    if(!lcd.lock || xSemaphoreTake(lcd.lock,0)!=pdTRUE) return;
    int64_t began=esp_timer_get_time();esp_err_t e=ESP_OK;
    if(atomic_exchange(&cancelled,false)) {
        lcd.config.mode=AGENT_DISPLAY_OFF;lcd.job++;lcd.pending=true;
    }
    if(lcd.config.mode==AGENT_DISPLAY_OFF || lcd.failed) {
        agent_err_t stopped=stop();
        if(stopped && stopped!=AGENT_ERR_BUSY) lcd.error=stopped;
        goto done;
    }
    if(lcd.phase==PANEL_OFF) {
        e=start();if(!e) {lcd.phase=PANEL_RESET;lcd.ready_at=(uint64_t)esp_timer_get_time()/1000+150;}
    } else if(lcd.phase==PANEL_RESET && now>=lcd.ready_at) {
        e=command(0x11,NULL,0);if(!e) {lcd.phase=PANEL_SLEEP;lcd.ready_at=(uint64_t)esp_timer_get_time()/1000+120;}
    } else if(lcd.phase==PANEL_SLEEP && now>=lcd.ready_at) {
        for(size_t i=0;!e && i<sizeof(panel_init)/sizeof(*panel_init);++i)
            e=command(panel_init[i].command,panel_init[i].data,panel_init[i].length);
        if(!e) {lcd.phase=PANEL_READY;lcd.row=0;lcd.last_second=-1;}
    } else if(lcd.phase==PANEL_READY) {
        int64_t epoch=(int64_t)time(NULL);
        bool due=lcd.row || lcd.pending || (lcd.config.mode==AGENT_DISPLAY_CLOCK && epoch!=lcd.last_second);
        if(due) {
            if(!lcd.row) {
                lcd.last_second=epoch;
                lcd.time_valid=agent_display_time(epoch,lcd.config.utc_offset,lcd.hms,lcd.zone);
            }
            /* At most four rows per existing control tick; no extra task or frame buffer. */
            for(unsigned i=0;!e && i<4;++i) {
                e=row(lcd.row);if(e) break;
                if(++lcd.row==AGENT_DISPLAY_HEIGHT) {
                    lcd.row=0;lcd.frames++;lcd.rendered_job=lcd.job;lcd.pending=false;break;
                }
            }
        }
    }
    if(e) {lcd.sdk_error=e;lcd.error=AGENT_ERR_TOOL;lcd.failed=true;lcd.pending=false;}
done:
    unsigned us=(unsigned)(esp_timer_get_time()-began);if(us>lcd.max_tick_us) lcd.max_tick_us=us;
    xSemaphoreGive(lcd.lock);
}
