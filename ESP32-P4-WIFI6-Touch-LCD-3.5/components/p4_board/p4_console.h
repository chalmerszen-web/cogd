#pragma once
#include "driver/uart.h"
#include "driver/uart_vfs.h"
/* Keep the existing command framing over the board's CH343 UART bridge. */
typedef struct {unsigned tx_buffer_size,rx_buffer_size;} usb_serial_jtag_driver_config_t;
static inline esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c)
{
    esp_err_t err=uart_driver_install(UART_NUM_0,(int)c->rx_buffer_size,16384,0,NULL,0);
    if(err==ESP_OK)err=uart_flush_input(UART_NUM_0);
    /* Binary NS startup printf bypasses esp_log_set_vprintf. Put stdout in
     * the same UART TX ring used by the CH343 command/event interface; direct
     * FIFO writes otherwise split already queued voice JSON mid-record. */
    if(err==ESP_OK)uart_vfs_dev_use_driver(UART_NUM_0);
#ifdef AGENT_KEYWORD_PCM
    /* Explicit diagnostic only: 31.25 hex PCM records/s exceed 115200 baud. */
    if(err==ESP_OK)err=uart_set_baudrate(UART_NUM_0,921600);
#endif
    return err;
}
static inline int usb_serial_jtag_read_bytes(void *p,size_t n,TickType_t ticks)
{return uart_read_bytes(UART_NUM_0,p,(uint32_t)n,ticks);}
static inline int usb_serial_jtag_write_bytes(const void *p,size_t n,TickType_t ticks)
{
    if(!ticks) {
        size_t available=0;
        if(uart_get_tx_buffer_free_size(UART_NUM_0,&available)!=ESP_OK || available<n+16)return 0;
    }
    /* Queue complete records: uart_tx_chars would truncate JSON at FIFO size. */
    return uart_write_bytes(UART_NUM_0,p,n);
}
