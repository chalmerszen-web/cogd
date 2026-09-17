#include "json.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

bool agent_utf8_valid(const char *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        uint32_t cp = (unsigned char)text[i++];
        if (cp < 0x80) continue;
        unsigned count;
        uint32_t minimum;
        if (cp >= 0xc2 && cp <= 0xdf) { count = 1; minimum = 0x80; cp &= 31; }
        else if (cp >= 0xe0 && cp <= 0xef) { count = 2; minimum = 0x800; cp &= 15; }
        else if (cp >= 0xf0 && cp <= 0xf4) { count = 3; minimum = 0x10000; cp &= 7; }
        else return false;
        if (count > length - i) return false;
        while (count--) {
            unsigned byte = (unsigned char)text[i++];
            if ((byte & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (byte & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

static bool valid_tree(const cJSON *value)
{
    if (cJSON_IsNumber(value) && !isfinite(value->valuedouble)) return false;
    if (cJSON_IsString(value) && !agent_utf8_valid(value->valuestring,strlen(value->valuestring))) return false;
    for (const cJSON *item = value->child; item; item = item->next) {
        if (cJSON_IsObject(value)) {
            if(!item->string || !agent_utf8_valid(item->string,strlen(item->string))) return false;
            for (const cJSON *other = item->next; other; other = other->next)
                if (!strcmp(item->string, other->string)) return false;
        }
        if (!valid_tree(item)) return false;
    }
    return true;
}

cJSON *agent_json_parse(const char *text, size_t length)
{
    if (!text || !length || length > AGENT_REQUEST_MAX || memchr(text, 0, length) ||
        !agent_utf8_valid(text, length)) return NULL;
    unsigned depth = 0, tokens = 0;
    bool quoted = false;
    for (size_t i = 0; i < length; ++i) {
        char ch = text[i];
        if (quoted && ch == '\\') {
            if (++i >= length) return NULL;
            /* cJSON strings cannot represent an embedded NUL. */
            if (text[i] == 'u' && length - i >= 5 && !memcmp(text + i, "u0000", 5)) return NULL;
            continue;
        }
        if (ch == '"') quoted = !quoted;
        if (quoted) { if ((unsigned char)ch < 0x20) return NULL; continue; }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            size_t n = i;
            if (text[n] == '-') ++n;
            if (n >= length) return NULL;
            if (text[n] == '0') ++n;
            else {
                if (text[n] < '1' || text[n] > '9') return NULL;
                while (n < length && text[n] >= '0' && text[n] <= '9') ++n;
            }
            if (n < length && text[n] == '.') {
                size_t first = ++n;
                while (n < length && text[n] >= '0' && text[n] <= '9') ++n;
                if (n == first) return NULL;
            }
            if (n < length && (text[n] == 'e' || text[n] == 'E')) {
                ++n;
                if (n < length && (text[n] == '+' || text[n] == '-')) ++n;
                size_t first = n;
                while (n < length && text[n] >= '0' && text[n] <= '9') ++n;
                if (n == first) return NULL;
            }
            if (n < length && !strchr(" \t\r\n,]}", text[n])) return NULL;
            i = n - 1;
        }
        if (ch == '{' || ch == '[') { if (++depth > 16) return NULL; }
        if (ch == '}' || ch == ']') { if (!depth--) return NULL; }
        if (ch == ',' || ch == ':' || ch == '[' || ch == '{')
            if (++tokens > 1024) return NULL;
    }
    if (quoted || depth) return NULL;
    const char *end = NULL;
    cJSON *value = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!value) return NULL;
    while (end < text + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    if (end != text + length || !valid_tree(value)) { cJSON_Delete(value); return NULL; }
    return value;
}

const char *agent_json_string(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

bool agent_json_uint(const cJSON *value, uint64_t maximum, uint64_t *out)
{
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < 0 ||
        value->valuedouble > (double)maximum || value->valuedouble >= 9007199254740992.0 ||
        floor(value->valuedouble) != value->valuedouble)
        return false;
    *out = (uint64_t)value->valuedouble;
    return true;
}

void agent_json_writer_init(agent_json_writer_t *writer, char *data, size_t capacity)
{
    *writer = (agent_json_writer_t){ .data = data, .capacity = capacity };
    if (capacity) data[0] = 0; else writer->error = AGENT_ERR_LIMIT;
}

static void append(agent_json_writer_t *writer, const char *data, size_t size)
{
    if (writer->error) return;
    if (size >= writer->capacity - writer->used) { writer->error = AGENT_ERR_LIMIT; return; }
    memcpy(writer->data + writer->used, data, size);
    writer->used += size;
    writer->data[writer->used] = 0;
}

void agent_json_raw(agent_json_writer_t *writer, const char *text) { append(writer, text, strlen(text)); }

agent_err_t agent_json_write_bytes(void *ctx,const char *data,size_t length)
{
    agent_json_writer_t *w=ctx; append(w,data,length); return w->error;
}

void agent_json_quote(agent_json_writer_t *writer, const char *text)
{
    if (!text || !agent_utf8_valid(text, strlen(text))) { writer->error = AGENT_ERR_JSON; return; }
    append(writer, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)text; *p && !writer->error; ++p) {
        char escape[7];
        if (*p == '"' || *p == '\\') { escape[0] = '\\'; escape[1] = (char)*p; append(writer, escape, 2); }
        else if (*p < 0x20) { snprintf(escape, sizeof(escape), "\\u%04x", *p); append(writer, escape, 6); }
        else append(writer, (const char *)p, 1);
    }
    append(writer, "\"", 1);
}

void agent_json_printf(agent_json_writer_t *writer, const char *format, ...)
{
    if (writer->error) return;
    va_list args;
    va_start(args, format);
    int size = vsnprintf(writer->data + writer->used, writer->capacity - writer->used, format, args);
    va_end(args);
    if (size < 0 || (size_t)size >= writer->capacity - writer->used) writer->error = AGENT_ERR_LIMIT;
    else writer->used += (size_t)size;
}
