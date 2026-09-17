#include "audio.h"
#include "tools.h"
#include "json.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned plays, volumes, microphones;
static agent_err_t play(void *ctx, const agent_score_t *score)
{ (void)ctx; assert(!agent_score_validate(score)); ++plays; return AGENT_OK; }
static agent_err_t volume(void *ctx, unsigned percent)
{ (void)ctx; assert(percent <= 100); ++volumes; return AGENT_OK; }
static agent_err_t microphone(void *ctx, bool enabled)
{ (void)ctx; assert(enabled); ++microphones; return AGENT_OK; }
static agent_err_t status(void *ctx, char *out, size_t n)
{ (void)ctx; return snprintf(out, n, "{\"accepted\":true}") < (int)n ? AGENT_OK : AGENT_ERR_LIMIT; }

int main(void)
{
    const char *score_json = "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[69,4],[0,2],[72,2]]}";
    agent_score_t score;
    assert(!agent_score_parse(score_json, &score));
    cJSON *node = agent_json_parse(score_json, strlen(score_json));
    agent_score_t direct;
    assert(node && !agent_score_parse_node(node, &direct));
    assert(!memcmp(&score, &direct, sizeof(score)));
    cJSON_Delete(node);
    assert(score.count == 3 && score.samples == AGENT_AUDIO_RATE);
    const char *bad[] = {
        "{}", "null", "[]",
        "{\"bpm\":0,\"wave\":\"sine\",\"notes\":[[60,4]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[35,4]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[97,4]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60,0]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60,17]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60.1,4]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[true,4]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60,4,1]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[]}",
        "{\"bpm\":120,\"wave\":\"noise\",\"notes\":[[60,4]]}",
        "{\"bpm\":120,\"wave\":\"sine\",\"notes\":[[60,4]],\"pin\":18}",
        "{\"bpm\":120,\"bpm\":80,\"wave\":\"sine\",\"notes\":[[60,4]]}"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        agent_score_t before = score;
        agent_err_t error = agent_score_parse(bad[i], &score);
        assert(error);
        node = agent_json_parse(bad[i], strlen(bad[i]));
        if (node) {
            assert(agent_score_parse_node(node, &score) == error);
            cJSON_Delete(node);
        }
        assert(!memcmp(&before, &score, sizeof(score)));
    }
    static int16_t whole[AGENT_AUDIO_RATE], fragmented[AGENT_AUDIO_RATE];
    agent_synth_t synth;
    assert(!agent_synth_init(&synth, &score));
    assert(agent_synth_render(&synth, whole, AGENT_AUDIO_RATE, 100) == AGENT_AUDIO_RATE);
    assert(!agent_synth_render(&synth, whole, 1, 100));
    unsigned crosses = 0, peak = 0;
    for (unsigned i = 0; i < AGENT_AUDIO_RATE; ++i) {
        unsigned value = (unsigned)(whole[i] < 0 ? -whole[i] : whole[i]);
        if (value > peak) peak = value;
        if (i && i < 12000 && whole[i - 1] <= 0 && whole[i] > 0) ++crosses;
        if (i >= 12000 && i < 18000) assert(whole[i] == 0);
    }
    assert(crosses >= 219 && crosses <= 221 && peak >= 8000 && peak <= 8192);
    assert(whole[0] == 0 && whole[11999] == 0 && whole[AGENT_AUDIO_RATE - 1] == 0);
    assert(!agent_synth_init(&synth, &score));
    unsigned offset = 0, seed = 7;
    while (offset < AGENT_AUDIO_RATE) {
        seed = seed * 1664525u + 1013904223u;
        unsigned chunk = 1 + seed % 701;
        if (chunk > AGENT_AUDIO_RATE - offset) chunk = AGENT_AUDIO_RATE - offset;
        offset += (unsigned)agent_synth_render(&synth, fragmented + offset, chunk, 100);
    }
    assert(!memcmp(whole, fragmented, sizeof(whole)));
    score.wave = AGENT_WAVE_TRIANGLE;
    assert(!agent_synth_init(&synth, &score));
    assert(agent_synth_render(&synth, whole, AGENT_AUDIO_RATE, 0) == AGENT_AUDIO_RATE);
    for (unsigned i = 0; i < AGENT_AUDIO_RATE; ++i) assert(whole[i] == 0);
    score.count = AGENT_SCORE_NOTES; score.bpm = 40;
    for (unsigned i = 0; i < score.count; ++i) score.notes[i] = (agent_note_t){60, 16};
    assert(agent_score_validate(&score) == AGENT_ERR_LIMIT);
    score.bpm = 137;
    for (unsigned i = 0; i < score.count; ++i) score.notes[i] = (agent_note_t){0, 1};
    assert(!agent_synth_init(&synth, &score));
    uint32_t samples = 0; size_t n;
    while ((n = agent_synth_render(&synth, whole, 240, 100))) samples += (uint32_t)n;
    assert(samples == (uint64_t)64 * AGENT_AUDIO_RATE * 60 / (137 * 4));
    agent_mic_meter_t meter = {0};
    for (unsigned i = 0; i < 1600; ++i) agent_mic_meter_feed(&meter, 2048);
    assert(!meter.peak && !agent_mic_meter_rms(&meter));
    memset(&meter, 0, sizeof(meter));
    for (unsigned i = 0; i < 1600; ++i)
        agent_mic_meter_feed(&meter, (uint16_t)(2048 + 100 * sin(i * 2 * 3.141592653589793 * 1000 / AGENT_MIC_RATE)));
    assert(agent_mic_meter_rms(&meter) > 1100 && agent_mic_meter_rms(&meter) < 1150);
    assert(!meter.clipped && meter.peak > 1500);
    agent_mic_meter_feed(&meter, 4095); assert(meter.clipped == 1);
    const agent_audio_ops_t audio = { .play = play, .volume = volume, .microphone = microphone, .status = status };
    const agent_tool_ops_t ops = { .audio = &audio };
    char output[128];
    assert(!agent_tool_invoke(&ops, "device.audio.play_score", score_json, output, sizeof(output)));
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(agent_tool_invoke(&ops, "device.audio.play_score", bad[i], output, sizeof(output)));
    assert(plays == 1);
    assert(!agent_tool_invoke(&ops, "device_audio_volume", "{\"percent\":20}", output, sizeof(output)));
    assert(agent_tool_invoke(&ops, "device_audio_volume", "{\"percent\":101}", output, sizeof(output)));
    assert(!agent_tool_invoke(&ops, "device_mic_set", "{\"enabled\":true}", output, sizeof(output)));
    assert(agent_tool_invoke(&ops, "device_mic_set", "{\"enabled\":1}", output, sizeof(output)));
    assert(volumes == 1 && microphones == 1 && AGENT_TOOLS_MAX == 4 && AGENT_TOOL_COUNT > AGENT_TOOLS_MAX);
    puts("audio: score validation, bounded synthesis, pitch/envelope/rests, fragmentation, meter and tools PASS");
    return 0;
}
