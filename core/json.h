#ifndef AGENT_JSON_H
#define AGENT_JSON_H
#include "agent.h"
#include "cJSON.h"

typedef struct { char *data; size_t used, capacity; agent_err_t error; } agent_json_writer_t;
bool agent_utf8_valid(const char *text, size_t length);
cJSON *agent_json_parse(const char *text, size_t length);
const char *agent_json_string(const cJSON *object, const char *key);
bool agent_json_uint(const cJSON *value, uint64_t maximum, uint64_t *out);
void agent_json_writer_init(agent_json_writer_t *writer, char *data, size_t capacity);
void agent_json_raw(agent_json_writer_t *writer, const char *text);
void agent_json_quote(agent_json_writer_t *writer, const char *text);
/* Synchronous quoted string, at most4096 bytes per write, no allocation.
 * Validate UTF8 before the first write; stop on the first sink error. The
 * caller owns immutable text until return and must discard partial output
 * on error. Useful for measured request producers without a second buffer. */
agent_err_t agent_json_write_quote(const char *text,agent_write_fn,void *);
void agent_json_printf(agent_json_writer_t *writer, const char *format, ...);
agent_err_t agent_json_write_bytes(void *writer,const char *data,size_t length);
#endif
