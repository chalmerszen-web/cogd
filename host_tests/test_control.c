#include "control.h"
#include "devices.h"
#include "json.h"
#include "tools.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static agent_resources_t resources;
static agent_control_t control;
static unsigned effects,stops;
static uint8_t light[3]={1,2,3};
static agent_pin_state_t pins[22];
static agent_err_t driver_error;
static bool pin_fail, hold_guard, two_pwm_channels;
#if AGENT_ENABLE_AUDIO
static agent_audio_state_t sound={.ready=true,.volume=20,.clip_storage=true};
static bool stuck;
static agent_err_t inspect(void *ctx,agent_audio_state_t *out) { (void)ctx; *out=sound; return AGENT_OK; }
static agent_err_t play(void *ctx,const agent_score_t *s)
{ (void)ctx; assert(!agent_score_validate(s)); if(sound.playing) return AGENT_ERR_BUSY; sound.playing=true; sound.play_error=AGENT_OK; ++effects; return AGENT_OK; }
static agent_err_t song_play(void *ctx,const agent_song_t *s)
{ (void)ctx; assert(!agent_song_validate(s)); if(sound.playing) return AGENT_ERR_BUSY; sound.playing=true; sound.play_error=AGENT_OK; ++effects; return AGENT_OK; }
static agent_err_t stop(void *ctx)
{ (void)ctx; ++stops; if(stuck) return AGENT_ERR_TIMEOUT; sound.playing=sound.recording=sound.listening=false; return AGENT_OK; }
static agent_err_t listen(void *ctx,bool enabled)
{ (void)ctx; if(!enabled) return stop(ctx); if(driver_error) return driver_error; sound.listening=true; return AGENT_OK; }
static agent_err_t mic(void *ctx,bool enabled)
{ (void)ctx; sound.mic_requested=sound.mic_enabled=enabled; ++effects; return AGENT_OK; }
static agent_err_t volume(void *ctx,unsigned value) { (void)ctx; sound.volume=value; return AGENT_OK; }
static agent_err_t capture(void *ctx,unsigned ms)
{ (void)ctx; sound.recording=true; sound.clip_ready=false; sound.clip_ms=ms; ++effects; return AGENT_OK; }
static agent_err_t replay(void *ctx)
{ (void)ctx; if(!sound.clip_ready) return AGENT_ERR_NOT_FOUND; sound.playing=true; sound.play_error=AGENT_OK; ++effects; return AGENT_OK; }
static const agent_audio_ops_t audio={.inspect=inspect,.play=play,.stop=stop,.microphone=mic,.volume=volume,.capture=capture,.replay=replay,.play_song=song_play,.listen=listen};
#endif
static agent_err_t light_get(void *ctx,uint8_t rgb[3]) { (void)ctx; memcpy(rgb,light,3); return AGENT_OK; }
static agent_err_t light_set(void *ctx,const uint8_t rgb[3]) { (void)ctx; if(driver_error) return driver_error; memcpy(light,rgb,3); ++effects; return AGENT_OK; }
static agent_err_t pin_get(void *ctx,unsigned pin,agent_pin_state_t *s) { (void)ctx; *s=pins[pin]; return AGENT_OK; }
static agent_err_t pin_set(void *ctx,unsigned pin,const agent_pin_state_t *s) { (void)ctx; if(pin_fail) return AGENT_ERR_TIMEOUT;
    if(two_pwm_channels && s->mode==AGENT_GPIO_PWM && pins[pin].mode!=AGENT_GPIO_PWM) {
        unsigned used=0; for(unsigned i=0;i<22;++i) used+=pins[i].mode==AGENT_GPIO_PWM;
        if(used>=2) return AGENT_ERR_BUSY;
    }
    pins[pin]=*s; ++effects; if(hold_guard) atomic_flag_test_and_set(&resources.guard); return AGENT_OK; }
static const agent_pin_t allowed[]={{0,AGENT_GPIO_READ,"button"},{4,7,"test output"},{5,7,"test output 2"},{10,7,"test output 3"}};
static agent_control_backend_t backend={.resources=&resources,.pins=allowed,.pin_count=4,
    .reserved_mask=AGENT_PIN(18)|AGENT_PIN(19)|AGENT_PIN(12),
    .light_mask=AGENT_RES_LIGHT|AGENT_PIN(8),.speaker_mask=AGENT_RES_SPEAKER|AGENT_PIN(6)|AGENT_PIN(7)|AGENT_PIN(3),
    .mic_mask=AGENT_RES_MIC|AGENT_PIN(2),.light_get=light_get,.light_set=light_set,.pin_get=pin_get,.pin_set=pin_set,
#if AGENT_ENABLE_AUDIO
    .audio=&audio,
#endif
};
static agent_plan_t parse(const char *json) { agent_plan_t plan; assert(!agent_plan_parse(json,&plan)); return plan; }
static void submit(const char *json) { agent_plan_t p=parse(json); assert(!agent_control_submit(&control,&p)); }
static void ended(agent_plan_status_t status)
{ assert(!atomic_load(&control.active)); assert(atomic_load(&control.status)==(unsigned)status); assert(!atomic_load(&resources.occupied)); }

static void direct_gpio_tests(void)
{
    agent_resources_init(&resources); agent_control_init(&control,&backend);
    effects=0; driver_error=AGENT_OK;
    agent_pin_state_t next={.mode=AGENT_GPIO_WRITE,.value=1},out;
    assert(!agent_control_gpio(&control,4,&next,&out) && effects==1 && out.value==1);
    assert(!agent_control_gpio(&control,4,NULL,&out) && effects==1 && out.value==1);
    assert(agent_control_gpio(&control,18,&next,&out)==AGENT_ERR_FORBIDDEN);
    assert(agent_control_gpio(&control,0,&next,&out)==AGENT_ERR_FORBIDDEN);
    atomic_flag_test_and_set(&control.admission);
    assert(agent_control_gpio(&control,4,&next,&out)==AGENT_ERR_BUSY);
    atomic_flag_clear(&control.admission);
    uint32_t added;
    assert(!agent_resources_claim(&resources,AGENT_OWNER_SYSTEM,AGENT_PIN(4),&added));
    assert(agent_control_gpio(&control,4,&next,&out)==AGENT_ERR_BUSY && effects==1);
    assert(!agent_resources_release(&resources,AGENT_OWNER_SYSTEM,added));
    pin_fail=true;
    assert(agent_control_gpio(&control,4,&next,&out)==AGENT_ERR_TIMEOUT);
    assert(!control.direct_held && !atomic_load(&resources.occupied));
    pin_fail=false; hold_guard=true;
    assert(!agent_control_gpio(&control,4,&next,&out) && effects==2);
    assert(control.direct_held==AGENT_PIN(4));
    assert(agent_control_gpio(&control,5,&next,&out)==AGENT_ERR_BUSY && effects==2);
    hold_guard=false; atomic_flag_clear(&resources.guard);
    agent_control_tick(&control,0);
    assert(!control.direct_held && !atomic_load(&resources.occupied) && effects==2);
    submit("{\"steps\":[{\"op\":\"gpio_write\",\"pin\":4,\"value\":0},{\"op\":\"wait\",\"ms\":100}]}");
    assert(agent_control_gpio(&control,4,NULL,&out)==AGENT_ERR_BUSY);
    agent_control_tick(&control,1);
    agent_control_cancel(&control); agent_control_tick(&control,2);
    assert(!atomic_load(&control.active) && pins[4].value==1);
    agent_tool_ops_t ops={.control=&control}; char text[512];
    assert(!agent_tool_invoke(&ops,"device_gpio_set","{\"pin\":4,\"mode\":\"pwm\",\"hz\":5000,\"duty\":1000}",text,sizeof(text)));
    assert(pins[4].mode==AGENT_GPIO_PWM);
    assert(!agent_tool_invoke(&ops,"device_gpio_get","{\"pin\":4}",text,sizeof(text)));
    assert(!agent_tool_invoke(&ops,"device_gpio_set","{\"pin\":4,\"mode\":\"input\"}",text,sizeof(text)));
    assert(pins[4].mode==AGENT_GPIO_READ);
    const char *bad[]={"{\"pin\":4,\"mode\":\"output\"}","{\"pin\":4,\"mode\":\"input\",\"value\":0}",
        "{\"pin\":4,\"mode\":\"pwm\",\"hz\":9,\"duty\":0}","{\"pin\":4,\"mode\":\"pwm\",\"hz\":5001,\"duty\":0}",
        "{\"pin\":4,\"mode\":\"pwm\",\"hz\":10,\"duty\":1001}","{\"pin\":4,\"mode\":\"output\",\"value\":true}",
        "{\"pin\":4,\"mode\":\"output\",\"value\":1,\"extra\":0}"};
    unsigned before=effects;
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)
        assert(agent_tool_invoke(&ops,"device_gpio_set",bad[i],text,sizeof(text))==AGENT_ERR_ARGUMENT && effects==before);
    effects=0; memset(pins,0,sizeof(pins));
}

static void pwm_restore_tests(void)
{
    for(unsigned cancel=0;cancel<2;++cancel) {
        agent_resources_init(&resources); agent_control_init(&control,&backend);
        memset(pins,0,sizeof(pins)); two_pwm_channels=true;
        pins[4]=(agent_pin_state_t){.mode=AGENT_GPIO_PWM,.hz=200,.duty=250};
        pins[10]=(agent_pin_state_t){.mode=AGENT_GPIO_PWM,.hz=300,.duty=750};
        pins[5]=(agent_pin_state_t){.mode=AGENT_GPIO_READ};
        agent_pin_state_t old4=pins[4],old5=pins[5],old10=pins[10];
        submit("{\"steps\":[{\"op\":\"gpio_write\",\"pin\":4,\"value\":0},{\"op\":\"pwm\",\"pin\":5,\"hz\":100,\"duty\":500},{\"op\":\"wait\",\"ms\":100}]}");
        agent_control_tick(&control,1000);
        assert(pins[4].mode==AGENT_GPIO_WRITE && pins[5].mode==AGENT_GPIO_PWM);
        if(cancel) agent_control_cancel(&control);
        agent_control_tick(&control,1100);
        assert(!atomic_load(&control.active) && !atomic_load(&resources.occupied));
        assert(atomic_load(&control.status)==(unsigned)(cancel?AGENT_PLAN_CANCELLED:AGENT_PLAN_DONE));
        assert(!memcmp(&old4,&pins[4],sizeof(old4)) && !memcmp(&old5,&pins[5],sizeof(old5)) && !memcmp(&old10,&pins[10],sizeof(old10)));
    }
    two_pwm_channels=false; effects=0; memset(pins,0,sizeof(pins));
}

static uint64_t status_clock;
static unsigned cancel_at;
static bool turn_cancelled,freeze_status_clock;
static uint64_t status_now(void *ctx) { (void)ctx;return freeze_status_clock?0:status_clock; }
static bool status_cancelled(void *ctx) { (void)ctx;return turn_cancelled; }
static void status_pause(unsigned ms)
{
    assert(ms && ms<=25);status_clock+=ms;
    if(cancel_at && status_clock>=cancel_at)turn_cancelled=true;
    agent_control_tick(&control,status_clock);
}
static void status_wait_tests(void)
{
    const agent_tool_ops_t ops={.control=&control,.now_ms=status_now,
        .wait_ms=status_pause,.cancelled=status_cancelled};
    char output[512];
    agent_resources_init(&resources);agent_control_init(&control,&backend);
    effects=0;driver_error=AGENT_OK;status_clock=0;cancel_at=0;turn_cancelled=false;
    submit("{\"steps\":[{\"op\":\"light\",\"r\":0,\"g\":0,\"b\":255},{\"op\":\"wait\",\"ms\":5000},{\"op\":\"light\",\"r\":0,\"g\":255,\"b\":0},{\"op\":\"wait\",\"ms\":5000},{\"op\":\"light\",\"r\":0,\"g\":0,\"b\":255}]}");
    assert(!agent_tool_invoke(&ops,"device_control_status","{}",output,sizeof(output)));
    assert(status_clock>=10000 && status_clock<15000 && effects==4);
    assert(strstr(output,"\"state\":\"done\"") && strstr(output,"\"active\":false"));
    assert(strstr(output,"\"light\":{\"r\":1,\"g\":2,\"b\":3}"));
    ended(AGENT_PLAN_DONE);
    status_clock=0;
    submit("{\"steps\":[{\"op\":\"wait\",\"ms\":30000}],\"timeout_ms\":40000}");
    assert(!agent_tool_invoke(&ops,"device.control.status","{\"wait_ms\":0}",output,sizeof(output)));
    assert(!status_clock && strstr(output,"\"active\":true"));
    assert(!agent_tool_invoke(&ops,"device_control_status","{}",output,sizeof(output)));
    assert(status_clock==15000 && strstr(output,"\"active\":true"));
    assert(!agent_tool_invoke(&ops,"device_control_status","{\"wait_ms\":37}",output,sizeof(output)));
    assert(status_clock==15037);
    const char *bad[]={"{\"wait_ms\":15001}","{\"wait_ms\":-1}","{\"wait_ms\":true}",
        "{\"wait_ms\":1.5}","{\"wait_ms\":0,\"extra\":0}","{\"wait_ms\":0,\"wait_ms\":1}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)
        assert(agent_tool_invoke(&ops,"device_control_status",bad[i],output,sizeof(output))!=AGENT_OK);
    assert(status_clock==15037);
    cancel_at=15087;
    assert(agent_tool_invoke(&ops,"device_control_status","{}",output,sizeof(output))==AGENT_ERR_CANCELLED);
    assert(status_clock==cancel_at && atomic_load(&control.active));
    agent_control_cancel(&control);agent_control_tick(&control,status_clock);ended(AGENT_PLAN_CANCELLED);
    cancel_at=0;turn_cancelled=false;status_clock=0;
    submit("{\"steps\":[{\"op\":\"wait\",\"ms\":30000}],\"timeout_ms\":40000}");
    freeze_status_clock=true;
    assert(!agent_tool_invoke(&ops,"device_control_status","{}",output,sizeof(output)));
    assert(status_clock==15000 && strstr(output,"\"active\":true"));
    freeze_status_clock=false;
    agent_control_cancel(&control);agent_control_tick(&control,status_clock);ended(AGENT_PLAN_CANCELLED);
    status_clock=0;
    submit("{\"steps\":[{\"op\":\"wait\",\"ms\":1000}],\"timeout_ms\":1000}");
    agent_tool_ops_t immediate=ops;immediate.wait_ms=NULL;
    assert(!agent_tool_invoke(&immediate,"device_control_status","{}",output,sizeof(output)) && !status_clock);
    agent_control_cancel(&control);
    assert(!agent_tool_invoke(&ops,"device_control_status","{}",output,sizeof(output)));
    assert(strstr(output,"\"state\":\"cancelled\""));
    puts("control status: timed sequence completion, bounded pending, immediate read, precise wait, cancellation and stalled clock PASS");
}

int main(void)
{
    status_wait_tests();
    direct_gpio_tests();
    pwm_restore_tests();
    agent_resources_init(&resources); agent_control_init(&control,&backend);
    uint32_t added=0,mask; unsigned worst,owner;
    assert(!agent_resources_claim(&resources,AGENT_OWNER_SYSTEM,AGENT_PIN(18),&added));
    assert(added==AGENT_PIN(18));
    assert(agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(18)|AGENT_PIN(4),&added)==AGENT_ERR_BUSY);
    assert(!agent_resources_owner(&resources,4,&owner) && !owner);
    assert(agent_resources_release(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(18))==AGENT_ERR_FORBIDDEN);
    assert(!agent_resources_release(&resources,AGENT_OWNER_SYSTEM,AGENT_PIN(18)));
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(4),&added) && added==AGENT_PIN(4));
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(4)|AGENT_PIN(5),&added) && added==AGENT_PIN(5));
    assert(!agent_resources_release(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(4)|AGENT_PIN(5)));
    const char *invalid[]={"{}","[]","null","{\"steps\":[]}",
        "{\"steps\":[{\"op\":\"wait\",\"ms\":1}],\"repeat\":9}",
        "{\"steps\":[{\"op\":\"wait\",\"ms\":0}]}",
        "{\"steps\":[{\"op\":\"light\",\"r\":1,\"g\":2,\"b\":3,\"\":1}]}",
        "{\"steps\":[{\"op\":\"gpio_write\",\"pin\":4,\"value\":true}]}",
        "{\"steps\":[{\"op\":\"pwm\",\"pin\":4,\"hz\":5001,\"duty\":200}]}",
        "{\"steps\":[{\"op\":\"exec\",\"code\":\"gpio(18)\"}]}",
        "{\"steps\":[{\"op\":\"wait_gpio\",\"pin\":0,\"value\":1,\"timeout_ms\":100,\"on_timeout\":\"retry\"}]}",
        "{\"steps\":[{\"op\":\"wait\",\"ms\":1,\"ms\":2}]}"};
    agent_plan_t plan=parse("{\"steps\":[{\"op\":\"wait\",\"ms\":100}]}");
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        agent_plan_t before=plan; assert(agent_plan_parse(invalid[i],&plan)); assert(!memcmp(&before,&plan,sizeof(plan)));
    }
    const char *bad_scores[]={"{\"steps\":[{\"op\":\"wait\",\"ms\":1}],\"scores\":[null]}","{\"steps\":[{\"op\":\"wait\",\"ms\":1}],\"scores\":[{}]}","{\"steps\":[{\"op\":\"wait\",\"ms\":1}],\"scores\":[{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60,0]]}]}","{\"steps\":[{\"op\":\"wait\",\"ms\":1}],\"scores\":[{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60,4]],\"extra\":1}]}"};
    for(unsigned i=0;i<sizeof(bad_scores)/sizeof(*bad_scores);++i) {
        agent_plan_t before=plan; assert(agent_plan_parse(bad_scores[i],&plan)==(AGENT_ENABLE_AUDIO?AGENT_ERR_ARGUMENT:AGENT_ERR_CONFIG));
        assert(!memcmp(&before,&plan,sizeof(plan)));
    }
    plan=parse("{\"steps\":[{\"op\":\"light\",\"r\":9,\"g\":8,\"b\":7},{\"op\":\"gpio_write\",\"pin\":18,\"value\":1}]}");
    assert(agent_control_submit(&control,&plan)==AGENT_ERR_FORBIDDEN && effects==0); /* No prefix effects. */
    plan=parse("{\"steps\":[{\"op\":\"gpio_write\",\"pin\":0,\"value\":1}]}");
    assert(agent_plan_validate(&plan,&backend,&mask,&worst)==AGENT_ERR_FORBIDDEN);
    plan=parse("{\"steps\":[{\"op\":\"wait\",\"ms\":40000}],\"repeat\":2}");
    assert(agent_plan_validate(&plan,&backend,&mask,&worst)==AGENT_ERR_LIMIT);
    plan=parse("{\"steps\":[{\"op\":\"pwm\",\"pin\":4,\"hz\":100,\"duty\":500},{\"op\":\"pwm\",\"pin\":5,\"hz\":100,\"duty\":500},{\"op\":\"pwm\",\"pin\":10,\"hz\":100,\"duty\":500}]}");
    assert(agent_plan_validate(&plan,&backend,&mask,&worst)==AGENT_ERR_LIMIT);
    plan=parse("{\"steps\":[{\"op\":\"light\",\"r\":9,\"g\":8,\"b\":7},{\"op\":\"wait\",\"ms\":100}],\"repeat\":2}");
    assert(!agent_plan_validate(&plan,&backend,&mask,&worst) && worst==200);
    assert(!agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(8),&added));
    assert(agent_control_submit(&control,&plan)==AGENT_ERR_BUSY && effects==0);
    assert(!agent_resources_release(&resources,AGENT_OWNER_DIRECT,added));
    assert(!agent_control_submit(&control,&plan));
    assert(agent_control_submit(&control,&plan)==AGENT_ERR_BUSY);
    agent_control_tick(&control,1000); assert(light[0]==9);
    assert(agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_PIN(8),&added)==AGENT_ERR_BUSY);
    agent_control_tick(&control,1100); assert(atomic_load(&control.cycles)==1);
    agent_control_tick(&control,1200); ended(AGENT_PLAN_DONE); assert(light[0]==1 && light[1]==2);
    submit("{\"steps\":[{\"op\":\"wait\",\"ms\":100}],\"timeout_ms\":100}");
    agent_control_tick(&control,1200); agent_control_tick(&control,1300); ended(AGENT_PLAN_DONE);
    /* Cancel before publication is consumed; no driver effects. */
    unsigned before=effects; assert(!agent_control_submit(&control,&plan)); agent_control_cancel(&control);
    agent_control_tick(&control,1300); ended(AGENT_PLAN_CANCELLED); assert(effects==before);
    pins[4]=(agent_pin_state_t){.mode=AGENT_GPIO_WRITE,.value=0};
    submit("{\"steps\":[{\"op\":\"pwm\",\"pin\":4,\"hz\":500,\"duty\":100},{\"op\":\"wait_gpio\",\"pin\":0,\"value\":1,\"timeout_ms\":500}]}");
    agent_control_tick(&control,2000); assert(pins[4].mode==AGENT_GPIO_PWM);
    agent_control_tick(&control,2100); assert(atomic_load(&control.active));
    pins[0].value=1; agent_control_tick(&control,2200); ended(AGENT_PLAN_DONE); assert(pins[4].mode==AGENT_GPIO_WRITE && !pins[4].value);
    pins[0].value=0;
    submit("{\"steps\":[{\"op\":\"wait_gpio\",\"pin\":0,\"value\":1,\"timeout_ms\":500}]}");
    agent_control_tick(&control,3000); agent_control_tick(&control,3500); ended(AGENT_PLAN_FAILED);
    assert(atomic_load(&control.error)==AGENT_ERR_TIMEOUT);
    submit("{\"steps\":[{\"op\":\"wait_gpio\",\"pin\":0,\"value\":1,\"timeout_ms\":500,\"on_timeout\":\"continue\"}]}");
    agent_control_tick(&control,4000); agent_control_tick(&control,4500); ended(AGENT_PLAN_DONE);
    assert(!agent_control_submit(&control,&plan)); agent_control_tick(&control,5000);
    driver_error=AGENT_ERR_TOOL; agent_control_cancel(&control); agent_control_tick(&control,5010);
    assert(atomic_load(&control.active) && atomic_load(&resources.occupied));
    driver_error=AGENT_OK; agent_control_tick(&control,5020); ended(AGENT_PLAN_CANCELLED);
#if AGENT_ENABLE_AUDIO
    const char *parallel="{\"scores\":[{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[69,4]]}],\"steps\":[{\"op\":\"play_score\",\"score\":0,\"wait\":false},{\"op\":\"light\",\"r\":20,\"g\":0,\"b\":0},{\"op\":\"await\",\"resource\":\"speaker\",\"timeout_ms\":1000}]}";
    sound.mic_requested=true;
    plan=parse("{\"steps\":[{\"op\":\"wait_level\",\"above\":500,\"timeout_ms\":1000},{\"op\":\"mic\",\"enabled\":false}],\"repeat\":2}");
    before=effects; assert(agent_control_submit(&control,&plan)==AGENT_ERR_ARGUMENT && effects==before);
    sound.mic_requested=false;
    plan=parse(parallel); assert(!agent_control_submit(&control,&plan)); agent_control_tick(&control,6000);
    assert(sound.playing && light[0]==20 && atomic_load(&control.step)==3);
    agent_control_tick(&control,6100); assert(atomic_load(&control.active));
    sound.playing=false; agent_control_tick(&control,6200); ended(AGENT_PLAN_DONE);
    plan.count=2; assert(agent_plan_validate(&plan,&backend,&mask,&worst)==AGENT_ERR_ARGUMENT); /* Unjoined sound. */
    submit(parallel); agent_control_tick(&control,7000); stuck=true; agent_control_cancel(&control);
    agent_control_tick(&control,7100); assert(atomic_load(&control.active) && sound.playing && stops);
    assert(agent_resources_claim(&resources,AGENT_OWNER_DIRECT,AGENT_RES_SPEAKER,&added)==AGENT_ERR_BUSY);
    stuck=false; agent_control_tick(&control,7200); ended(AGENT_PLAN_CANCELLED);
    submit("{\"steps\":[{\"op\":\"mic\",\"enabled\":true},{\"op\":\"wait_level\",\"above\":500,\"timeout_ms\":1000}]}");
    sound.mic_valid=true; sound.mic_rms=1000; sound.mic_updated_ms=7000;
    agent_control_tick(&control,8000); agent_control_tick(&control,8100); assert(atomic_load(&control.active)); /* Old RMS must not trigger. */
    sound.mic_updated_ms=8100; agent_control_tick(&control,8200); ended(AGENT_PLAN_DONE); assert(!sound.mic_requested);
    submit("{\"steps\":[{\"op\":\"capture\",\"ms\":500},{\"op\":\"replay\"}]}");
    agent_control_tick(&control,9000); assert(sound.recording && !sound.playing);
    sound.recording=false; sound.clip_ready=true; agent_control_tick(&control,10000); assert(sound.playing);
    sound.playing=false; agent_control_tick(&control,10600); ended(AGENT_PLAN_DONE);
    sound.clip_ready=false;
    plan=parse("{\"steps\":[{\"op\":\"replay\"}]}"); assert(agent_control_submit(&control,&plan)==AGENT_ERR_NOT_FOUND);
#endif
    char output[2048]; assert(!agent_control_capabilities(&control,output,sizeof(output)));
    cJSON *json=agent_json_parse(output,strlen(output)); assert(json); cJSON_Delete(json);
    assert(!agent_control_status(&control,output,sizeof(output))); json=agent_json_parse(output,strlen(output)); assert(json); cJSON_Delete(json);
    agent_devices_t devices; agent_devices_init(&devices,&backend,&control);
    const uint8_t rgb[]={30,40,50};
    assert(!agent_devices_light_set(&devices,rgb) && !atomic_load(&resources.occupied));
    submit("{\"steps\":[{\"op\":\"light\",\"r\":1,\"g\":2,\"b\":3},{\"op\":\"wait\",\"ms\":100}]}");
    before=effects; assert(agent_devices_light_set(&devices,rgb)==AGENT_ERR_BUSY && effects==before);
    agent_control_tick(&control,11000); agent_control_cancel(&control); agent_control_tick(&control,11010); ended(AGENT_PLAN_CANCELLED);
#if AGENT_ENABLE_AUDIO
    agent_score_t score; assert(!agent_score_parse("{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[69,4]]}",&score));
    const agent_audio_ops_t *a=&devices.audio;
    assert(!a->play(a->ctx,&score));
    assert(atomic_load(&resources.occupied)==backend.speaker_mask);
    assert(a->play(a->ctx,&score)==AGENT_ERR_BUSY);
    assert(!a->volume(a->ctx,35)); assert(atomic_load(&resources.occupied)==backend.speaker_mask);
    plan=parse(parallel); assert(agent_control_submit(&control,&plan)==AGENT_ERR_BUSY);
    assert(!a->stop(a->ctx) && !atomic_load(&resources.occupied));
    assert(!a->microphone(a->ctx,true)); assert(atomic_load(&resources.occupied)==backend.mic_mask);
    assert(agent_resources_claim(&resources,AGENT_OWNER_STORAGE,backend.mic_mask|backend.speaker_mask,&added)==AGENT_ERR_BUSY);
    assert(!a->capture(a->ctx,500));
    assert(atomic_load(&resources.occupied)==(backend.speaker_mask|backend.mic_mask|AGENT_RES_CLIP));
    assert(a->microphone(a->ctx,false)==AGENT_ERR_BUSY && sound.mic_requested);
    assert(a->play(a->ctx,&score)==AGENT_ERR_BUSY);
    sound.recording=false; sound.clip_ready=true; agent_devices_poll(&devices);
    assert(atomic_load(&resources.occupied)==backend.mic_mask); /* Preserve the preexisting mic lease. */
    assert(!a->microphone(a->ctx,false)); assert(!atomic_load(&resources.occupied));
    assert(!agent_resources_claim(&resources,AGENT_OWNER_STORAGE,backend.mic_mask|backend.speaker_mask,&added));
    assert(a->play(a->ctx,&score)==AGENT_ERR_BUSY && a->microphone(a->ctx,true)==AGENT_ERR_BUSY);
    assert(!agent_resources_release(&resources,AGENT_OWNER_STORAGE,added));
    assert(!a->replay(a->ctx)); assert(atomic_load(&resources.occupied)==(backend.speaker_mask|AGENT_RES_CLIP));
    sound.playing=false; agent_devices_poll(&devices); assert(!atomic_load(&resources.occupied));
    submit(parallel); agent_control_tick(&control,12000);
    assert(!a->stop(a->ctx) && atomic_load(&control.cancelled));
    assert(atomic_load(&control.active) && sound.playing); /* Cancellation publication is not cleanup completion. */
    agent_control_tick(&control,12010); ended(AGENT_PLAN_CANCELLED);
    agent_song_t song;
    assert(!agent_song_parse("{\"v\":1,\"bpm\":128,\"patterns\":[[[69,4,100,80]]],\"sequence\":[[0,-1,-1,-1,100]]}",&song));
    assert(!a->play_song(a->ctx,&song));
    assert(atomic_load(&resources.occupied)==backend.speaker_mask);
    assert(a->play(a->ctx,&score)==AGENT_ERR_BUSY);
    plan=parse(parallel); assert(agent_control_submit(&control,&plan)==AGENT_ERR_BUSY);
    stuck=true; assert(a->stop(a->ctx)==AGENT_ERR_TIMEOUT);
    assert(atomic_load(&resources.occupied)==backend.speaker_mask);
    stuck=false; assert(!a->stop(a->ctx)); assert(!atomic_load(&resources.occupied));
    /* Listening owns the whole acoustic path even between utterances. Failed
     * admission releases new claims; cancellation keeps claims until stopped. */
    driver_error=AGENT_ERR_MEMORY;
    assert(a->listen(a->ctx,true)==AGENT_ERR_MEMORY && !atomic_load(&resources.occupied));
    driver_error=AGENT_OK;
    assert(!a->listen(a->ctx,true));
    uint32_t listening_mask=backend.speaker_mask|backend.mic_mask|AGENT_RES_CLIP;
    assert(atomic_load(&resources.occupied)==listening_mask);
    agent_devices_poll(&devices); assert(atomic_load(&resources.occupied)==listening_mask);
    assert(!agent_devices_light_set(&devices,rgb));
    plan=parse(parallel); assert(agent_control_submit(&control,&plan)==AGENT_ERR_BUSY);
    sound.recording=true;
    assert(a->capture(a->ctx,500)==AGENT_ERR_BUSY);
    stuck=true; assert(a->listen(a->ctx,false)==AGENT_ERR_TIMEOUT);
    assert(atomic_load(&resources.occupied)==listening_mask);
    stuck=false; assert(!a->listen(a->ctx,false) && !atomic_load(&resources.occupied));
    assert(!a->listen(a->ctx,true));
    sound.listening=false; /* Backend initialization failure is reaped. */
    agent_devices_poll(&devices); assert(!atomic_load(&resources.occupied));
#endif
    puts("control: preflight, ownership, parallel audio, fresh triggers, repeat, timeout, cancel and restoration passed");
}
