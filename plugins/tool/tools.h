#ifndef AGENT_TOOLS_H
#define AGENT_TOOLS_H
#include "llm.h"
#include "control.h"
#include "display.h"
#ifndef AGENT_ENABLE_AUDIO
#define AGENT_ENABLE_AUDIO 0
#endif
#if AGENT_ENABLE_AUDIO
#include "audio.h"
#define AGENT_TOOL_COUNT 25u
#else
#define AGENT_TOOL_COUNT 17u
#endif

typedef struct {
    agent_err_t (*status)(void *ctx, char *output, size_t capacity);
    agent_err_t (*light_get)(void *ctx, uint8_t rgb[3]);
    agent_err_t (*light_set)(void *ctx, const uint8_t rgb[3]);
    agent_err_t (*context_stats)(void *ctx, char *output, size_t capacity);
    agent_err_t (*context_search)(void *,const char *,uint64_t,unsigned,char *,size_t);
    agent_err_t (*summary_get)(void *,char *,size_t);
    agent_err_t (*summary_set)(void *,const char *,uint64_t);
    uint64_t (*now_ms)(void *ctx);
    void *ctx;
    agent_control_t *control;
    const char *hardware_prompt;
    agent_err_t (*hardware)(void *,char *,size_t);
    const agent_display_ops_t *display;
#if AGENT_ENABLE_AUDIO
    const agent_audio_ops_t *audio;
#endif
} agent_tool_ops_t;

typedef struct { const char *name, *wire_name; } agent_tool_description_t;
extern const agent_tool_description_t agent_tool_descriptions[AGENT_TOOL_COUNT];
agent_err_t agent_tools_write(void *,agent_write_fn,void *);
const agent_tool_description_t *agent_tool_find(const char *name);
agent_err_t agent_tools_validate(const agent_llm_reply_t *reply);
/* Validate without effects and describe rejection for a model repair turn. */
agent_err_t agent_tool_check(const char *,const char *,char *,size_t);
agent_err_t agent_tool_invoke(const agent_tool_ops_t *ops, const char *name, const char *arguments,
                             char *output, size_t capacity);
#endif
