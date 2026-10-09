#ifndef AGENT_TCP_PROBE_H
#define AGENT_TCP_PROBE_H
#include <stddef.h>
/* Diagnostic only. Both descriptors must be sockets owned by this voice
 * turn and remain open until the synchronous snapshot returns. HTTP is
 * published only while on_sent waits for the candidate owner to join. */
typedef struct {
    int fd,available,mailbox;
    unsigned local,remote,matches,state,window,announced,next;
    unsigned refused_bytes,refused_pbufs,ooo_bytes,ooo_pbufs;
    unsigned unsent,unacked,retries;
} esp_agent_tcp_side_t;
typedef struct {
    unsigned at,duration;
    int error;
    esp_agent_tcp_side_t side[2]; /* candidate WS, formal HTTP */
} esp_agent_tcp_snapshot_t;
void esp_agent_tcp_snapshot(int ws,int http,esp_agent_tcp_snapshot_t *);
void esp_agent_tcp_format(const esp_agent_tcp_snapshot_t *,unsigned side,char *,size_t);
#endif
