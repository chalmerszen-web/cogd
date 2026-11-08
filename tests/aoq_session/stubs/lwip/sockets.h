#pragma once
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
extern int64_t test_now;
inline unsigned test_udp_packets=0,test_udp_packet_ms=0;
inline int test_socket(int,int,int){return 7;}
inline int test_connect(int,const sockaddr*,socklen_t){return 0;}
inline int test_ioctl(int,unsigned long,int*){return 0;}
inline int test_close(int){return 0;}
inline int test_send(int,const void*,size_t n,int){return static_cast<int>(n);}
inline int test_recv(int,void* p,size_t,int){
 if(test_udp_packets){--test_udp_packets;test_now+=test_udp_packet_ms;*static_cast<uint8_t*>(p)=0;return 1;}
 errno=EAGAIN;return -1;
}
#define socket test_socket
#define connect test_connect
#define ioctl test_ioctl
#define close test_close
#define send test_send
#define recv test_recv
