#ifndef TEST_NETCONN_H
#define TEST_NETCONN_H
struct sys_mbox_s {unsigned os_mbox;};
struct netconn {
    union {struct tcp_pcb *tcp;} pcb;
    union {int socket;} callback_arg;
    struct sys_mbox_s *recvmbox;
};
#endif
