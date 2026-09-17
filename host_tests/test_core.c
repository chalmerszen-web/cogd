#include "agent.h"
#include <assert.h>
#include <stdio.h>

static int initialized, stopped;
static agent_err_t init_ok(void *ctx) { (void)ctx; ++initialized; return AGENT_OK; }
static agent_err_t start_fail(void *ctx) { (void)ctx; return AGENT_ERR_CONFIG; }
static void deinit_ok(void *ctx) { (void)ctx; --initialized; }
static void stop_ok(void *ctx) { (void)ctx; ++stopped; }

int main(void)
{
    agent_core_t core;
    agent_core_init(&core);
    agent_plugin_t a = AGENT_PLUGIN(AGENT_INPUT, "input.test", NULL, NULL);
    a.init = init_ok; a.deinit = deinit_ok; a.stop = stop_ok;
    assert(agent_register(&core, &a) == AGENT_OK);
    assert(agent_register(&core, &a) == AGENT_ERR_DUPLICATE);
    agent_plugin_t b = AGENT_PLUGIN(AGENT_OUTPUT, "output.test", NULL, NULL);
    b.abi_major = AGENT_ABI_MAJOR - 1;
    assert(agent_register(&core, &b) == AGENT_ERR_ABI);
    b.abi_major = AGENT_ABI_MAJOR; b.struct_size = 0;
    assert(agent_register(&core, &b) == AGENT_ERR_ABI);
    b.struct_size = sizeof(b); b.init = init_ok; b.deinit = deinit_ok; b.start = start_fail;
    assert(agent_register(&core, &b) == AGENT_OK);
    assert(agent_start(&core) == AGENT_ERR_CONFIG);
    assert(initialized == 0 && stopped == 1);
    b.start = NULL;
    assert(agent_start(&core) == AGENT_OK);
    assert(initialized == 2 && agent_find(&core, "output.test") == &b);
    assert(agent_begin_turn(&core) == AGENT_OK);
    assert(agent_begin_turn(&core) == AGENT_ERR_BUSY);
    for (unsigned i = 0; i < AGENT_ROUNDS_MAX; ++i) assert(agent_next_round(&core) == AGENT_OK);
    assert(agent_next_round(&core) == AGENT_ERR_LIMIT);
    agent_cancel(&core);
    assert(agent_next_round(&core) == AGENT_ERR_CANCELLED);
    agent_end_turn(&core);
    assert(agent_begin_turn(&core) == AGENT_OK);
    assert(!atomic_load(&core.cancelled));
    agent_stop(&core);
    assert(initialized == 0);
    puts("core: registry, ABI, rollback, busy, cancellation and round limits PASS");
    return 0;
}
