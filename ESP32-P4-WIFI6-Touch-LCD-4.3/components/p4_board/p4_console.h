#pragma once
#include "driver/uart.h"
/* Keep the existing command framing over the board's CH343 UART bridge. */
typedef struct {unsigned tx_buffer_size,rx_buffer_size;} usb_serial_jtag_driver_config_t;
static inline esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c)
{ return uart_driver_install(UART_NUM_0,(int)c->rx_buffer_size,(int)c->tx_buffer_size,0,NULL,0); }
static inline int usb_serial_jtag_read_bytes(void *p,size_t n,TickType_t ticks)
{return uart_read_bytes(UART_NUM_0,p,(uint32_t)n,ticks);}
static inline int usb_serial_jtag_write_bytes(const void *p,size_t n,TickType_t ticks)
{return ticks?uart_write_bytes(UART_NUM_0,p,n):uart_tx_chars(UART_NUM_0,p,(uint32_t)n);}
