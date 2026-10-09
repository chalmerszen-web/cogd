#ifndef TEST_TCPIP_H
#define TEST_TCPIP_H
typedef int err_t;
err_t tcpip_callback_wait(void (*fn)(void *),void *);
#endif
