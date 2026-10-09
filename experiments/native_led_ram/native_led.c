/* Freestanding ESP32-C3, four WS2812 LEDs on ESP-Hi GPIO8.
 * RV32IMC machine code, no FreeRTOS, ESP-IDF libraries, heap or Flash calls.
 * Reference: ESP32-C3 TRM v1.4, chapters 5, 10, 30 and 33:
 * https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf
 * Register encodings cross-checked against Espressif's ESP-IDF 6.1 headers.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define BIT(n) (1u << (n))
#define SYS_CLK REG(0x600c0010u)
#define SYS_RST REG(0x600c0018u)
#define SYSTIMER_CONF REG(0x60023000u)
#define SYSTIMER_OP REG(0x60023004u)
#define SYSTIMER_LO REG(0x60023044u)
#define USB_FIFO REG(0x60043000u)
#define USB_CONF REG(0x60043004u)
#define RMT_CONF REG(0x60016010u)
#define RMT_RAW REG(0x60016038u)
#define RMT_ENA REG(0x60016040u)
#define RMT_CLR REG(0x60016044u)
#define RMT_SYS REG(0x60016068u)
#define RMT_MEM ((volatile uint32_t *)(uintptr_t)0x60016400u)
#define TICKS_SECOND 16000000u /* XTAL 40 MHz / fixed 2.5 divider */
#define INTERVAL (3u * TICKS_SECOND)
#define CH_CONFIG ((3u << 16) | (4u << 8) | BIT(6))

extern void disable_default_watchdog(void);
static uint32_t frame_count;

static uint32_t ticks(void)
{
    /* Clear previous validity, request a new cross-clock snapshot. */
    SYSTIMER_OP = BIT(29);
    SYSTIMER_OP = BIT(30);
    while (!(SYSTIMER_OP & BIT(29))) {}
    return SYSTIMER_LO;
}

static void until(uint32_t deadline)
{
    while ((int32_t)(ticks() - deadline) < 0) {}
}

static void tx(char ch)
{
    uint32_t start = ticks();
    while (!(USB_CONF & BIT(1))) {
        if (ticks() - start > TICKS_SECOND / 50u) return;
    }
    USB_FIFO = (uint8_t)ch;
}

static void text(const char *s)
{
    while (*s) tx(*s++);
}

static void number(uint32_t value)
{
    char digits[10];
    unsigned n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (n) tx(digits[--n]);
}

static void end_line(void)
{
    text("}\n");
    /* Full packets auto-flush. Ensure a final short/zero packet is sent. */
    uint32_t start = ticks();
    while (!(USB_CONF & BIT(1))) {
        if (ticks() - start > TICKS_SECOND / 50u) return;
    }
    USB_CONF = BIT(0);
}

__attribute__((noreturn)) static void idle(void)
{
    for (;;) __asm__ volatile ("nop");
}

__attribute__((noreturn)) void native_trap(uint32_t cause, uint32_t pc,
                                          uint32_t value)
{
    text("{\"event\":\"trap\",\"cause\":"); number(cause);
    text(",\"pc\":"); number(pc);
    text(",\"value\":"); number(value); end_line();
    idle();
}

static void setup(void)
{
    disable_default_watchdog();
    SYS_CLK |= BIT(29) | BIT(9);
    SYS_RST |= BIT(9);
    SYS_RST &= ~(BIT(29) | BIT(9));
    SYSTIMER_CONF = BIT(31) | BIT(30) | BIT(0);

    /* RMT XTAL source / 1, channel / 4: 10 MHz, one tick = 100 ns.
     * Three adjacent blocks give CH0 144 symbols; other channels stay idle.
     */
    RMT_SYS = BIT(31) | BIT(26) | (3u << 24) | BIT(3) | BIT(1) | BIT(0);
    RMT_ENA = 0;
    RMT_CLR = 0xffffffffu;
    RMT_CONF = CH_CONFIG;
    RMT_CONF = CH_CONFIG | BIT(24);

    REG(0x6000400cu) = BIT(8);       /* GPIO output latch initially low */
    REG(0x60004094u) = 0;            /* GPIO8 push-pull, no interrupts */
    REG(0x60009024u) = (1u << 12) | (2u << 10); /* GPIO mux, drive 20 mA */
    REG(0x60004574u) = 51u | BIT(9); /* RMT_CH0 output, GPIO enable */
    REG(0x60004024u) = BIT(8);
}

static int color(uint8_t r, uint8_t g, uint8_t b)
{
    /* Board's established GRB order and WS2812 timings. */
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    unsigned at = 0;
    for (unsigned led = 0; led < 4; ++led) {
        for (int bit = 23; bit >= 0; --bit) {
            RMT_MEM[at++] = (grb & BIT(bit))
                ? (9u | BIT(15) | (3u << 16))
                : (3u | BIT(15) | (9u << 16));
        }
    }
    RMT_MEM[at++] = 1400u | (1400u << 16); /* Reset/latch low for 280 us */
    RMT_MEM[at] = 0;                      /* Hardware end marker */
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    RMT_CLR = 0xffffffffu;
    RMT_CONF = CH_CONFIG | BIT(1);
    RMT_CONF = CH_CONFIG;
    RMT_CONF = CH_CONFIG | BIT(2);
    RMT_CONF = CH_CONFIG;
    RMT_CONF = CH_CONFIG | BIT(24);
    RMT_CONF = CH_CONFIG | BIT(0);
    uint32_t start = ticks();
    while (!(RMT_RAW & (BIT(0) | BIT(4)))) {
        if (ticks() - start > TICKS_SECOND / 200u) return 0;
    }
    if (RMT_RAW & BIT(4)) return 0;
    ++frame_count;
    return 1;
}

__attribute__((noreturn)) static void fail(const char *reason)
{
    text("{\"event\":\"error\",\"reason\":\""); text(reason);
    text("\",\"rmt_raw\":"); number(RMT_RAW);
    text(",\"rmt_conf\":"); number(RMT_CONF); end_line();
    (void)color(0, 0, 0);
    idle();
}

static void wait_go(void)
{
    uint32_t start = ticks(), next = start;
    unsigned match = 0;
    while (ticks() - start < 30u * TICKS_SECOND) {
        if ((int32_t)(ticks() - next) >= 0) {
            text("{\"event\":\"ready\",\"isa\":\"rv32imc\",\"gpio\":8,\"leds\":4,");
            text("\"timer_hz\":16000000,\"interval_s\":3,\"duration_s\":60");
            end_line();
            next += TICKS_SECOND;
        }
        while (USB_CONF & BIT(2)) {
            uint8_t ch = (uint8_t)USB_FIFO;
            if (ch == (uint8_t)"GO\n"[match]) {
                if (++match == 3) return;
            } else match = ch == 'G' ? 1u : 0u;
        }
    }
    fail("host_go_timeout");
}

void native_main(void)
{
    setup();
    if (!color(0, 0, 0)) fail("initial_rmt_tx");
    wait_go();
    const uint8_t palette[3][3] = {{64, 0, 0}, {0, 64, 0}, {0, 0, 64}};
    const char *names[3] = {"red", "green", "blue"};
    uint32_t start = ticks(), first_complete = 0;
    for (unsigned step = 0; step < 20; ++step) {
        until(start + step * INTERVAL);
        unsigned idx = step % 3u;
        if (!color(palette[idx][0], palette[idx][1], palette[idx][2]))
            fail("rmt_tx");
        uint32_t complete = ticks();
        if (!step) first_complete = complete;
        text("{\"event\":\"step\",\"step\":"); number(step);
        text(",\"color\":\""); text(names[idx]);
        text("\",\"ticks\":"); number(complete - start);
        text(",\"frames\":"); number(frame_count); end_line();
    }
    until(start + 20u * INTERVAL);
    if (!color(0, 0, 0)) fail("final_rmt_tx");
    uint32_t off = ticks();
    text("{\"event\":\"done\",\"ticks\":"); number(off - start);
    text(",\"visible_ticks\":"); number(off - first_complete);
    text(",\"frames\":"); number(frame_count);
    text(",\"off\":true"); end_line();
    idle(); /* Host verifies Flash and resets back into preserved firmware. */
}

