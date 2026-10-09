/* RAM-only LCD isolation probe. Touches only known LCD GPIO4/5/10 and
 * USB/SYSTIMER. No SDK, Flash writes, eFuses, audio, motors or network.
 * GPIO register layout: ESP32-C3 TRM and local IDF v6.1 soc headers.
 * Panel register values: pinned ESP-HI BSP, third_party/esp-hi-lcd/LICENSE.
 */
#include <stdint.h>
#include <stdbool.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define BIT(n) (1u << (n))
#define MOSI BIT(4)
#define SCLK BIT(5)
#define DC BIT(10)
#define PINS (MOSI|SCLK|DC)
#define SET REG(0x60004008u)
#define CLR REG(0x6000400cu)
#define IN REG(0x6000403cu)
#define USB_FIFO REG(0x60043000u)
#define USB_CONF REG(0x60043004u)
#define TIMER_OP REG(0x60023004u)
#define TIMER_LO REG(0x60023044u)
#define SECOND 16000000u
extern void disable_default_watchdog(void);

static uint32_t ticks(void)
{
    TIMER_OP=BIT(29);TIMER_OP=BIT(30);
    while(!(TIMER_OP&BIT(29))) {}
    return TIMER_LO;
}
static void delay_us(uint32_t us)
{
    uint32_t t=ticks();
    while(ticks()-t<us*16u) {}
}
static void send(const char *s)
{
    while(*s) {
        uint32_t t=ticks();
        while(!(USB_CONF&BIT(1))) if(ticks()-t>SECOND/50) return;
        USB_FIFO=(uint8_t)*s++;
    }
    USB_CONF=BIT(0);
}
static void hex(uint32_t n)
{
    char s[11];s[0]='0';s[1]='x';s[10]=0;
    for(unsigned i=0;i<8;i++) s[9-i]="0123456789abcdef"[(n>>(4*i))&15];
    send(s);
}
static void pins_init(void)
{
    const unsigned pins[]={4,5,10};
    for(unsigned i=0;i<3;i++) {
        unsigned p=pins[i];
        /* GPIO function, input enabled for pad readback, drive=2; no pulls. */
        REG(0x60009004u+4*p)=BIT(12)|BIT(9)|(2u<<10);
        REG(0x60004554u+4*p)=BIT(9)|128u;
    }
    CLR=PINS;REG(0x60004024u)=PINS;
    delay_us(10);
    send("{\"type\":\"pads\",\"low\":\"");hex(IN&PINS);
    SET=PINS;delay_us(10);
    send("\",\"high\":\"");hex(IN&PINS);send("\"}\n");
    CLR=PINS;
}
static void byte(uint8_t b)
{
    for(unsigned i=0;i<8;i++) {
        if(b&128) SET=MOSI;else CLR=MOSI;
        delay_us(1);SET=SCLK;delay_us(1);CLR=SCLK;b<<=1;
    }
}
static void cmd(uint8_t value,const uint8_t *data,unsigned n)
{
    CLR=DC;byte(value);SET=DC;
    for(unsigned i=0;i<n;i++) byte(data[i]);
}
static void init(void)
{
    static const struct {uint8_t cmd,n,data[16];} sequence[]={
        {0xb1,3,{5,0x3a,0x3a}},{0xb2,3,{5,0x3a,0x3a}},
        {0xb3,6,{5,0x3a,0x3a,5,0x3a,0x3a}},{0xb4,1,{3}},
        {0xc0,3,{0x44,4,4}},{0xc1,1,{0xc0}},{0xc2,2,{0x0d,0}},
        {0xc3,2,{0x8d,0x6a}},{0xc4,2,{0x8d,0xee}},{0xc5,1,{8}},
        {0xe0,16,{0x0f,0x10,3,3,7,2,0,2,7,0x0c,0x13,0x38,0x0a,0x0e,3,0x10}},
        {0xe1,16,{0x10,0x0b,4,4,0x10,3,0,3,3,9,0x17,0x33,0x0b,0x0c,6,0x10}},
        {0x35,1,{0}},{0x3a,1,{5}},{0x36,1,{0xa8}},
        {0x20,0,{0}},{0x29,0,{0}}
    };
    cmd(1,0,0);delay_us(150000);cmd(0x11,0,0);delay_us(120000);
    for(unsigned i=0;i<sizeof(sequence)/sizeof(sequence[0]);i++)
        cmd(sequence[i].cmd,sequence[i].data,sequence[i].n);
}
static void fill(bool bands)
{
    const uint8_t col[]={0,0,0,159},row[]={0,24,0,103};
    const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    cmd(0x2a,col,4);cmd(0x2b,row,4);cmd(0x2c,0,0);
    for(unsigned y=0;y<80;y++) for(unsigned x=0;x<160;x++) {
        uint16_t c=bands?colors[x/40]:0xffff;
        byte((uint8_t)(c>>8));byte((uint8_t)c);
    }
}
void native_trap(uint32_t cause,uint32_t pc,uint32_t value)
{
    (void)value;send("{\"type\":\"trap\",\"cause\":\"");hex(cause);
    send("\",\"pc\":\"");hex(pc);send("\"}\n");
    for(;;) __asm__ volatile("nop");
}
void native_main(void)
{
    disable_default_watchdog();
    REG(0x600c0010u)|=BIT(29);REG(0x600c0018u)&=~BIT(29);
    REG(0x60023000u)=BIT(31)|BIT(30)|BIT(0);
    uint32_t last=ticks()-SECOND;
    bool started=false;
    for(;;) {
        if(ticks()-last>=SECOND) {
            send(started?"{\"type\":\"hold\",\"pattern\":\"bands\"}\n":
                         "{\"type\":\"ready\",\"probe\":\"lcd_gpio_bitbang\"}\n");last=ticks();
        }
        while(USB_CONF&BIT(2)) if((uint8_t)USB_FIFO=='G' && !started) {
            pins_init();init();fill(false);
            send("{\"type\":\"drawn\",\"pattern\":\"white\"}\n");
            delay_us(3000000);
            fill(true);send("{\"type\":\"drawn\",\"pattern\":\"bands\"}\n");
            started=true;
        }
    }
}
