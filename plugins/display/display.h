#ifndef AGENT_DISPLAY_H
#define AGENT_DISPLAY_H
#include "agent.h"

enum { AGENT_DISPLAY_WIDTH=160, AGENT_DISPLAY_HEIGHT=80 };
typedef enum { AGENT_DISPLAY_OFF, AGENT_DISPLAY_CLOCK, AGENT_DISPLAY_FILL } agent_display_mode_t;
typedef struct { agent_display_mode_t mode; int utc_offset; uint16_t foreground,background; } agent_display_config_t;
typedef struct {
    agent_err_t (*set)(void *,const agent_display_config_t *);
    agent_err_t (*status)(void *,char *,size_t);
    void *ctx;
} agent_display_ops_t;

agent_err_t agent_display_parse(const char *,agent_display_config_t *);
bool agent_display_time(int64_t epoch,int offset,char hms[9],char zone[10]);
/* One RGB565 big-endian row; never a full-frame allocation. */
void agent_display_row(const agent_display_config_t *,const char hms[9],const char zone[10],unsigned y,uint8_t row[320]);
#endif
