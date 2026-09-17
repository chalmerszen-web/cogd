#ifndef AGENT_PORT_CONTRACT_H
#define AGENT_PORT_CONTRACT_H
#include "agent.h"
#include "transport.h"
#include "context.h"

/* Contract only; M0 links no model runtime and allocates no runtime state. */
typedef struct {
    agent_err_t (*probe)(void *ctx,size_t *required_ram);
    agent_err_t (*load)(void *ctx,const char *model);
    agent_err_t (*infer)(void *ctx,const char *input,agent_emit_fn emit,void *output_ctx);
    void (*unload)(void *ctx);
    void *ctx;
} agent_model_runtime_ops_t;

/* OpenWrt and JieLi supply these adapters against their exact SDK/SKU. */
typedef struct {
    agent_platform_ops_t platform;
    agent_transport_ops_t transport;
    agent_flash_ops_t flash;
    agent_input_ops_t input;
    agent_output_ops_t output;
} agent_port_contract_t;
#endif
