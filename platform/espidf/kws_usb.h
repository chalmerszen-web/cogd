#ifndef AGENT_KWS_USB_H
#define AGENT_KWS_USB_H
#include <stdbool.h>
#include <stddef.h>
bool kws_usb_begin(void);
void kws_usb_end(void);
/* Sequential 256-sample PCM16-LE blocks with a mandatory CRC32. */
const char *kws_usb_frame(unsigned sequence,unsigned crc,const char *hex);
/* Bounded standalone frontend diagnostic; requires an already reserved session. */
const char *kws_usb_pcen_check(void);
#endif
