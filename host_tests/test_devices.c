#include "devices.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static agent_resources_t resources;
static agent_devices_t devices;
static unsigned effects;
static uint8_t light[3];
static bool hold_cleanup,change_owner;
static agent_err_t driver_error;
static const uint32_t light_mask=AGENT_RES_LIGHT|AGENT_PIN(8);

static agent_err_t set(void *ctx,const uint8_t rgb[3])
{
    (void)ctx;
    if(!driver_error) { memcpy(light,rgb,3); ++effects; }
    /* Inject the interleaving after driver admission, before resource reap. */
    if(hold_cleanup) assert(!atomic_flag_test_and_set(&resources.guard));
    if(change_owner) {
        uint32_t added;
        assert(!agent_resources_release(&resources,AGENT_OWNER_DIRECT,light_mask));
        assert(!agent_resources_claim(&resources,AGENT_OWNER_SYSTEM,light_mask,&added));
    }
    return driver_error;
}
#if AGENT_ENABLE_AUDIO
static agent_audio_state_t sound;
static agent_err_t inspect_error;
static agent_err_t inspect(void *ctx,agent_audio_state_t *out)
{ (void)ctx; *out=sound; return inspect_error; }
static agent_err_t stop(void *ctx)
{
    (void)ctx; sound.playing=false; ++effects;
    if(hold_cleanup) assert(!atomic_flag_test_and_set(&resources.guard));
    return AGENT_OK;
}
static const agent_audio_ops_t audio={.inspect=inspect,.stop=stop};
#endif

static void released(unsigned count)
{
    agent_devices_poll(&devices);
    assert(!devices.held && !atomic_load(&resources.occupied) && effects==count);
}

int main(void)
{
    agent_control_backend_t backend={.resources=&resources,.light_mask=light_mask,.light_set=set,
        .speaker_mask=AGENT_RES_SPEAKER|AGENT_PIN(6),
#if AGENT_ENABLE_AUDIO
        .audio=&audio,
#endif
    };
    agent_resources_init(&resources); agent_devices_init(&devices,&backend,NULL);
    const uint8_t rgb[]={9,8,7};
    uint32_t added; unsigned owner;

    /* Pre-effect contention must still reject without a partial action. */
    assert(!atomic_flag_test_and_set(&devices.guard));
    assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_BUSY && !effects);
    atomic_flag_clear(&devices.guard);
    assert(!atomic_flag_test_and_set(&resources.guard));
    assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_BUSY && !effects);
    atomic_flag_clear(&resources.guard);
    assert(!agent_resources_claim(&resources,AGENT_OWNER_PLAN,light_mask,&added));
    assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_BUSY && !effects);
    assert(!agent_resources_release(&resources,AGENT_OWNER_PLAN,light_mask));

    /* A successful effect stays successful; its lease protects deferred reap. */
    hold_cleanup=true;
    assert(agent_devices_light_set(&devices,rgb)==AGENT_OK);
    assert(effects==1 && !memcmp(light,rgb,3));
    agent_devices_poll(&devices); /* Still contended: leave ownership intact. */
    assert(devices.held==light_mask && atomic_load(&resources.occupied)==light_mask);
    atomic_flag_clear(&resources.guard);
    assert(!agent_resources_owner(&resources,8,&owner) && owner==AGENT_OWNER_DIRECT);
    assert(agent_resources_claim(&resources,AGENT_OWNER_PLAN,light_mask,&added)==AGENT_ERR_BUSY);
    released(1); released(1); /* Reap is idempotent and never repeats the driver. */

    /* Cleanup contention does not replace a real driver failure. */
    driver_error=AGENT_ERR_TOOL;
    assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_TOOL && effects==1);
    assert(devices.held==light_mask);
    atomic_flag_clear(&resources.guard); released(1);
    driver_error=AGENT_OK; hold_cleanup=false;

    /* A non-transient cleanup error is visible; never steal another owner. */
    change_owner=true;
    assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_FORBIDDEN && effects==2);
    assert(!agent_resources_owner(&resources,8,&owner) && owner==AGENT_OWNER_SYSTEM);
    assert(devices.held==light_mask);
    change_owner=false;
    assert(!agent_resources_release(&resources,AGENT_OWNER_SYSTEM,light_mask));
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DIRECT,light_mask,&added));
    released(2);

#if AGENT_ENABLE_AUDIO
    /* An accepted stop and an unavailable post-action status use the same rule. */
    sound.playing=true;
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DIRECT,backend.speaker_mask,&added));
    devices.held=added; hold_cleanup=true;
    assert(!devices.audio.stop(devices.audio.ctx) && !sound.playing && effects==3);
    assert(devices.held==backend.speaker_mask);
    atomic_flag_clear(&resources.guard); hold_cleanup=false; released(3);
    inspect_error=AGENT_ERR_BUSY;
    assert(!agent_devices_light_set(&devices,rgb) && effects==4);
    assert(devices.held==light_mask);
    inspect_error=AGENT_OK; released(4);
    inspect_error=AGENT_ERR_TIMEOUT;
    assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_TIMEOUT && effects==5);
    assert(devices.held==light_mask);
    inspect_error=AGENT_OK; released(5);
    /* Audio preflight status contention still prevents the stop call. */
    inspect_error=AGENT_ERR_BUSY;
    assert(devices.audio.stop(devices.audio.ctx)==AGENT_ERR_BUSY && effects==5);
    inspect_error=AGENT_OK; released(5);
#endif
    puts("devices: admission isolation, accepted effects, deferred ownership, original failures and hard cleanup errors passed");
}
