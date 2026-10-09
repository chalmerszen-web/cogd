#include "audio.h"
#include "json.h"
#include <string.h>

/* DDS increments at 24 kHz for MIDI 36..96; no floating point in the render loop. */
static const uint32_t phase_steps[] = {
    11704930, 12400941, 13138339, 13919586, 14747287, 15624207,
    16553270, 17537579, 18580418, 19685267, 20855814, 22095965,
    23409859, 24801882, 26276679, 27839171, 29494575, 31248413,
    33106541, 35075158, 37160835, 39370534, 41711627, 44191930,
    46819719, 49603764, 52553357, 55678342, 58989149, 62496826,
    66213081, 70150316, 74321671, 78741067, 83423255, 88383859,
    93639437, 99207528, 105106715, 111356685, 117978298, 124993653,
    132426162, 140300631, 148643341, 157482134, 166846509, 176767719,
    187278874, 198415056, 210213429, 222713370, 235956596, 249987305,
    264852324, 280601263, 297286682, 314964268, 333693018, 353535438,
    374557749
};
static const int16_t sine_quarter[] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179,
    7962, 8739, 9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732,
    15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403,
    22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571,
    30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
    32609, 32678, 32728, 32757, 32767
};

static uint32_t total_samples(const agent_score_t *score)
{
    uint32_t ticks = 0;
    for (unsigned i = 0; i < score->count; ++i) ticks += score->notes[i].ticks;
    return (uint32_t)((uint64_t)ticks * AGENT_AUDIO_RATE * 60 / (score->bpm * 4));
}

agent_err_t agent_score_validate(const agent_score_t *score)
{
    if (!score || score->bpm < 40 || score->bpm > 240 || score->wave > AGENT_WAVE_TRIANGLE ||
        score->wave < AGENT_WAVE_SINE || !score->count) return AGENT_ERR_ARGUMENT;
    if (score->count > AGENT_SCORE_NOTES) return AGENT_ERR_LIMIT;
    for (unsigned i = 0; i < score->count; ++i) {
        const agent_note_t *n = &score->notes[i];
        if ((n->midi && (n->midi < 36 || n->midi > 96)) || !n->ticks || n->ticks > 16)
            return AGENT_ERR_ARGUMENT;
    }
    return total_samples(score) > AGENT_AUDIO_RATE * AGENT_SCORE_SECONDS ? AGENT_ERR_LIMIT : AGENT_OK;
}

agent_err_t agent_score_parse_node(const struct cJSON *root, agent_score_t *score)
{
    if (!root || !score) return AGENT_ERR_ARGUMENT;
    agent_score_t candidate = {0};
    agent_err_t error = AGENT_ERR_ARGUMENT;
    uint64_t bpm;
    const char *wave = agent_json_string(root, "wave");
    const cJSON *notes = cJSON_GetObjectItemCaseSensitive(root, "notes");
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 3 || !wave ||
        !agent_json_uint(cJSON_GetObjectItemCaseSensitive(root, "bpm"), 240, &bpm) ||
        !cJSON_IsArray(notes)) goto done;
    candidate.bpm = (uint16_t)bpm;
    if (!strcmp(wave, "sine")) candidate.wave = AGENT_WAVE_SINE;
    else if (!strcmp(wave, "triangle")) candidate.wave = AGENT_WAVE_TRIANGLE;
    else goto done;
    for (const cJSON *note = notes->child; note; note = note->next) {
        if (candidate.count == AGENT_SCORE_NOTES) { error = AGENT_ERR_LIMIT; goto done; }
        uint64_t midi, ticks;
        if (!cJSON_IsArray(note) || cJSON_GetArraySize(note) != 2 ||
            !agent_json_uint(note->child, 96, &midi) ||
            !agent_json_uint(note->child->next, 16, &ticks)) goto done;
        candidate.notes[candidate.count++] = (agent_note_t){(uint8_t)midi, (uint8_t)ticks};
    }
    error = agent_score_validate(&candidate);
    if (!error) { candidate.samples = total_samples(&candidate); *score = candidate; }
done:
    return error;
}

agent_err_t agent_score_parse(const char *json, agent_score_t *score)
{
    if (!json || !score) return AGENT_ERR_ARGUMENT;
    size_t length = strlen(json);
    if (length > AGENT_ARGS_MAX) return AGENT_ERR_LIMIT;
    cJSON *root = agent_json_parse(json, length);
    if (!root) return AGENT_ERR_JSON;
    agent_err_t error = agent_score_parse_node(root, score);
    cJSON_Delete(root);
    return error;
}

static void start_note(agent_synth_t *s)
{
    uint32_t numerator = s->score->notes[s->note].ticks * AGENT_AUDIO_RATE * 60 + s->remainder;
    uint32_t divisor = s->score->bpm * 4;
    s->note_samples = numerator / divisor;
    s->remainder = numerator % divisor;
    s->phase = s->position = 0;
}

agent_err_t agent_synth_init(agent_synth_t *s, const agent_score_t *score)
{
    if (!s) return AGENT_ERR_ARGUMENT;
    agent_err_t error = agent_score_validate(score);
    if (error) return error;
    *s = (agent_synth_t){ .score = score };
    start_note(s);
    return AGENT_OK;
}

static int32_t wave_sample(uint32_t phase, agent_wave_t wave)
{
    uint32_t position = phase >> 16;
    if (wave == AGENT_WAVE_TRIANGLE)
        return (int32_t)((position < 32768 ? position : 65535 - position) * 2) - 32767;
    int sign = position >= 32768 ? -1 : 1;
    position &= 32767;
    if (position > 16384) position = 32768 - position;
    unsigned index = position >> 8, fraction = position & 255;
    int32_t value = sine_quarter[index];
    if (index < 64) value += (sine_quarter[index + 1] - value) * (int32_t)fraction / 256;
    return sign * value;
}

uint32_t agent_audio_phase_step(unsigned midi)
{ return midi>=36 && midi<=96?phase_steps[midi-36]:0; }
int32_t agent_audio_sine(uint32_t phase) { return wave_sample(phase,AGENT_WAVE_SINE); }

size_t agent_cue_render(agent_cue_t *c,int16_t *pcm,size_t capacity,unsigned volume)
{
    if(!c || !pcm || (c->rate!=16000 && c->rate!=24000))return 0;
    unsigned length=c->rate*(c->finish?18u:16u)/100,attack=c->rate/100;
    if(c->sample>=length)return 0;
    size_t count=length-c->sample;if(count>capacity)count=capacity;
    if(volume>80)volume=80;
    for(size_t i=0;i<count;++i,++c->sample) {
        unsigned n=c->sample,hz=c->finish?2300-1400*n/length:1320;
        c->phase+=(uint32_t)(((uint64_t)hz<<32)/c->rate);
        int32_t value=agent_audio_sine(c->phase);
        value=value*(int32_t)(n<attack?n:attack)/(int32_t)attack;
        value=value*(int32_t)(length-n)/(int32_t)length;
        pcm[i]=(int16_t)(value*(int32_t)volume/(c->finish?320:960));
    }
    return count;
}

size_t agent_synth_render(agent_synth_t *s, int16_t *pcm, size_t capacity, unsigned volume)
{
    if (!s || !s->score || !pcm || volume > 100) return 0;
    size_t n = 0;
    while (n < capacity && s->note < s->score->count) {
        uint8_t midi = s->score->notes[s->note].midi;
        int32_t sample = midi ? wave_sample(s->phase, s->score->wave) : 0;
        /* Ten-ms attack/release; a quarter-scale ceiling precedes master volume. */
        uint32_t envelope = s->position < 240 ? s->position : 240;
        uint32_t remaining = s->note_samples - s->position - 1;
        if (remaining < envelope) envelope = remaining;
        sample = sample * (int32_t)envelope / 240;
        pcm[n++] = (int16_t)(sample * (int32_t)volume / 400);
        if (midi) s->phase += phase_steps[midi - 36];
        ++s->rendered;
        if (++s->position == s->note_samples && ++s->note < s->score->count) start_note(s);
    }
    return n;
}

void agent_mic_meter_feed(agent_mic_meter_t *m, uint16_t raw)
{
    if (!m || raw > 4095) return;
    int32_t input = (int32_t)raw * 256;
    if (!m->initialized) { m->bias_q8 = input; m->initialized = true; }
    m->bias_q8 += (input - m->bias_q8) / 256;
    int32_t sample = (input - m->bias_q8) / 16;
    if (sample < 0) sample = -sample;
    if (sample > 32767) sample = 32767;
    m->squares += (uint64_t)sample * (uint32_t)sample;
    if ((uint32_t)sample > m->peak) m->peak = (uint32_t)sample;
    m->clipped += raw <= 3 || raw >= 4092;
    ++m->count;
}

uint32_t agent_mic_meter_rms(const agent_mic_meter_t *m)
{
    if (!m || !m->count) return 0;
    uint32_t value = (uint32_t)(m->squares / m->count), result = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return result;
}
