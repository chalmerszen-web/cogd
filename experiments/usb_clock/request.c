/* Deterministic RAM-only ESP32-C3 USB request. No network, Flash or GPIO writes.
 * Register definitions follow the verified native_led_ram transport and C3 TRM. */
#include <stdint.h>
#include <stdbool.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define BIT(n) (1u << (n))
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
static bool send(const char *text)
{
    while(*text) {
        uint32_t began=ticks();
        while(!(USB_CONF&BIT(1))) if(ticks()-began>SECOND/50) return false;
        USB_FIFO=(uint8_t)*text++;
    }
    USB_CONF=BIT(0);
    return true;
}
void native_trap(uint32_t cause,uint32_t pc,uint32_t value)
{
    (void)cause;(void)pc;(void)value;
    send("{\"type\":\"trap\"}\n");
    for(;;) __asm__ volatile("nop");
}
void native_main(void)
{
    disable_default_watchdog();
    REG(0x600c0010u)|=BIT(29);
    REG(0x600c0018u)&=~BIT(29);
    REG(0x60023000u)=BIT(31)|BIT(30)|BIT(0);
    unsigned match=0;uint32_t last=ticks()-SECOND;
    for(;;) {
        if(ticks()-last>=SECOND) {
            send("{\"type\":\"ready\",\"id\":\"clock-20261010-01\"}\n");last=ticks();
        }
        while(USB_CONF&BIT(2)) {
            uint8_t c=(uint8_t)USB_FIFO;
            if(c==(uint8_t)"GO\n"[match]) {
                if(++match==3) {
                    send("{\"type\":\"codex_request\",\"id\":\"clock-20261010-01\",\"text\":\"我要变成一个时钟，24小时制，显示时、分，右下角小字显示秒。\"}\n");
                    for(;;) __asm__ volatile("nop");
                }
            } else match=c=='G'?1u:0u;
        }
    }
}
