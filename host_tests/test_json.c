#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *text = "\"hello\"\\\r\n\t中文🙂";
    char output[256];
    agent_json_writer_t w;
    agent_json_writer_init(&w, output, sizeof(output));
    agent_json_raw(&w, "{\"text\":"); agent_json_quote(&w, text); agent_json_raw(&w, "}");
    assert(w.error == AGENT_OK);
    cJSON *json = agent_json_parse(output, w.used);
    assert(json && !strcmp(agent_json_string(json, "text"), text)); cJSON_Delete(json);
    const char *bad[] = {"{", "{\"a\":1,\"a\":2}", "{\"a\":01}", "{\"a\":1.}",
        "{\"a\":1e}", "{\"a\":1e999}", "{\"x\":\"\\u0000\"}", "{\"x\":\"\n\"}",
        "{} trailing", "[1,]", "{\"x\":\"\\ud800\"}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        json = agent_json_parse(bad[i], strlen(bad[i]));
        if (json) fprintf(stderr, "incorrectly accepted: %s\n", bad[i]);
        assert(!json);
    }
    assert(!agent_utf8_valid("\xc0\x80", 2));
    assert(!agent_utf8_valid("\xf0\x9f", 2));
    assert(!agent_utf8_valid("\xed\xa0\x80", 3));
    char deep[40]; memset(deep, '[', 20); memset(deep + 20, ']', 20);
    assert(!agent_json_parse(deep, sizeof(deep)));
    agent_json_writer_init(&w, output, 3); agent_json_quote(&w, "abcd");
    assert(w.error == AGENT_ERR_LIMIT);
    puts("json: escaping, UTF-8, strict numbers, duplicates, depth and size bounds PASS");
    return 0;
}
