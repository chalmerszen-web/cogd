/* Reuse the known-pin RAM probe; deliberately omit its SCLK pad-test edge.
 * Each host N adds one edge, covering all eight possible byte boundaries.
 * No Flash writes, unidentified GPIOs or OTP commands. */
#define native_main original_native_main
#define pins_init original_pins_init
#include "probe.c"
#undef pins_init
#undef native_main

static void clean_pins_init(void)
{
    const unsigned pins[]={4,5,10};
    /* Preload low before enabling any output. GPIO5 is never pulled high. */
    CLR=PINS;
    for(unsigned i=0;i<3;i++) {
        unsigned p=pins[i];
        REG(0x60009004u+4*p)=BIT(12)|BIT(9)|(2u<<10);
        REG(0x60004554u+4*p)=BIT(9)|128u;
    }
    REG(0x60004024u)=PINS;
    delay_us(10);
    send("{\"type\":\"pads_low\",\"value\":\"");hex(IN&PINS);send("\"}\n");
}

static void full_gram_bands(void)
{
    /* Cover all ST7735S GRAM after MV=1, eliminating the visible-window offset. */
    const uint8_t col[]={0,0,0,161},row[]={0,0,0,131};
    const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    cmd(0x2a,col,4);cmd(0x2b,row,4);cmd(0x2c,0,0);
    for(unsigned y=0;y<132;y++) for(unsigned x=0;x<162;x++) {
        uint16_t c=colors[x*4/162];
        byte((uint8_t)(c>>8));byte((uint8_t)c);
    }
}

void native_main(void)
{
    disable_default_watchdog();
    REG(0x600c0010u)|=BIT(29);REG(0x600c0018u)&=~BIT(29);
    REG(0x60023000u)=BIT(31)|BIT(30)|BIT(0);
    uint32_t last=ticks()-SECOND;
    unsigned phase=0;bool started=false;
    for(;;) {
        if(ticks()-last>=SECOND) {
            if(!started) send("{\"type\":\"ready\",\"probe\":\"lcd_phase\"}\n");
            last=ticks();
        }
        while(USB_CONF&BIT(2)) {
            uint8_t input=(uint8_t)USB_FIFO;
            if(input=='G' && !started) {clean_pins_init();started=true;}
            else if(input=='N' && started && phase<7) {
                CLR=DC|MOSI;delay_us(2);SET=SCLK;delay_us(2);CLR=SCLK;phase++;
            } else continue;
            /* Flush any partial previous byte with NOPs before reset. */
            cmd(0,0,0);cmd(0,0,0);init();full_gram_bands();
            char number[2]={(char)('0'+phase),0};
            send("{\"type\":\"drawn\",\"pattern\":\"full_gram_bands\",\"phase\":");
            send(number);send("}\n");
        }
    }
}
