#include "p4_aoq.h"
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET_ESP32P4) || !CONFIG_IDF_TARGET_ESP32P4
#error "AOQ allocation is an ESP32-P4-only component"
#endif
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "../../../plugins/speech/audio_session.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int esp_agent_qianwen_auth(char *, size_t);

namespace {
/* Match core/agent.h without importing its C11-only types into C++. */
enum Error { Ok=0, Argument=1, Limit=5, JsonError=6, Protocol=7, Network=8,
             AuthError=11, Forbidden=12, Rate=13, Server=14, Timeout=15, Memory=19 };

void wipe(void *data, size_t length) {
    auto p=static_cast<volatile unsigned char *>(data);
    while(length--) *p++=0;
}
struct Auth {
    char text[224]{};
    ~Auth() { wipe(text,sizeof(text)); }
};
struct Body {
    char *data=static_cast<char *>(calloc(P4_AOQ_RESPONSE_MAX+1u,1));
    ~Body() { if(data) {wipe(data,P4_AOQ_RESPONSE_MAX+1u);free(data);} }
};
void wipe_json(cJSON *item) {
    for(auto p=item;p;p=p->next) {
        if(p->valuestring) wipe(p->valuestring,strlen(p->valuestring));
        if(p->child) wipe_json(p->child);
    }
}
struct Json {
    cJSON *root=nullptr;
    ~Json() { wipe_json(root);cJSON_Delete(root); }
};
struct Http {
    esp_http_client_handle_t handle=nullptr;
    ~Http() {
        if(handle) {
            /* The HTTP client owns a copy; remove it before freeing the handle. */
            char *header=nullptr;
            if(esp_http_client_get_header(handle,"Authorization",&header)==ESP_OK && header)
                wipe(header,strlen(header));
            esp_http_client_delete_header(handle,"Authorization");
            esp_http_client_cleanup(handle);
        }
    }
};

const cJSON *unique(const cJSON *object,const char *name) {
    if(!cJSON_IsObject(object))return nullptr;
    const cJSON *found=nullptr;
    for(auto p=object->child;p;p=p->next) {
        if(p->string && !strcmp(p->string,name)) {
            if(found)return nullptr;
            found=p;
        }
    }
    return found;
}
bool copy_string(const cJSON *object,const char *name,char *out,size_t cap) {
    const auto p=unique(object,name);
    if(!cJSON_IsString(p) || !p->valuestring)return false;
    size_t n=strlen(p->valuestring);
    if(!n || n>=cap)return false;
    for(size_t i=0;i<n;++i)if(static_cast<unsigned char>(p->valuestring[i])<0x21 ||
                               static_cast<unsigned char>(p->valuestring[i])>0x7e)return false;
    memcpy(out,p->valuestring,n+1);return true;
}
bool number(const cJSON *object,const char *name,uint32_t low,uint32_t high,uint32_t &out) {
    const auto p=unique(object,name);
    if(!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) ||
       p->valuedouble<low || p->valuedouble>high || std::floor(p->valuedouble)!=p->valuedouble)return false;
    out=static_cast<uint32_t>(p->valuedouble);return true;
}
int hex(unsigned char c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
bool fingerprint(const cJSON *root,uint8_t out[32]) {
    const auto item=unique(root,"clientRelayCertFingerprint");
    if(!cJSON_IsString(item) || !item->valuestring)return false;
    const char *s=item->valuestring;
    if(!strncmp(s,"sha256/",7))s+=7;
    if(strlen(s)!=64)return false;
    for(size_t i=0;i<32;++i) {
        int a=hex(s[i*2]),b=hex(s[i*2+1]);
        if(a<0 || b<0)return false;
        out[i]=static_cast<uint8_t>((a<<4)|b);
    }
    return true;
}
bool host(const char *s) {
    /* No scheme, path, whitespace, userinfo, percent escapes or brackets. */
    bool colon=false;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(s);*p;++p) {
        if(*p==':') {colon=true;continue;}
        if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
             (*p>='0' && *p<='9') || *p=='-' || *p=='.'))return false;
    }
    if(colon) {
        unsigned colons=0;
        for(const char *p=s;*p;++p) {if(*p==':')++colons;else if(*p!='.' && hex(*p)<0)return false;}
        return colons>=2; /* Full address parsing remains getaddrinfo's job. */
    }
    return s[0]!='.' && s[0]!='-' && s[strlen(s)-1]!='-';
}

int parse(const char *body,size_t length,p4_aoq_kind_t kind,p4_aoq_allocation_t *out) {
    p4_aoq_allocation_clear(out);
    if(!body || !length || length>P4_AOQ_RESPONSE_MAX || memchr(body,0,length))return Protocol;
    /* cJSON strings are NUL-terminated. Reject escaped NUL too, instead of
     * silently validating a truncated token/host. Reject overdeep JSON before
     * parsing so cJSON never consumes an unbounded embedded task stack. */
    unsigned depth=0;bool quoted=false,escaped=false;
    for(size_t i=0;i<length;++i) {
        char c=body[i];
        if(quoted) {
            if(escaped) {if(c=='u' && i+4<length && !memcmp(body+i+1,"0000",4))return Protocol;escaped=false;}
            else if(c=='\\')escaped=true;
            else if(c=='"')quoted=false;
        } else if(c=='"')quoted=true;
        else if(c=='{' || c=='[') {if(++depth>8)return Protocol;}
        else if(c=='}' || c==']') {if(!depth)return Protocol;--depth;}
    }
    Json json;
    const char *end=nullptr;
    json.root=cJSON_ParseWithLengthOpts(body,length,&end,false);
    if(!json.root)return JsonError;
    while(end<body+length && (*end==' ' || *end=='\t' || *end=='\r' || *end=='\n'))++end;
    if(end!=body+length || !cJSON_IsObject(json.root))return Protocol;
    p4_aoq_allocation_t value{};
    struct Clear {p4_aoq_allocation_t *p;~Clear(){p4_aoq_allocation_clear(p);}} clear{&value};
    if(!copy_string(json.root,"sid",value.sid,sizeof(value.sid)) ||
       !copy_string(json.root,"aoqTokenForClient",value.token,sizeof(value.token)) ||
       !fingerprint(json.root,value.fingerprint_sha256) ||
       !number(json.root,"sidExpiresInSecs",1,UINT32_MAX,value.expires_in_seconds))return Protocol;
    const auto extra=unique(json.root,"extraInfo");
    if(!copy_string(extra,"workspaceIdHash",value.workspace,sizeof(value.workspace)))return Protocol;
    const auto endpoints=unique(json.root,"clientRelayEndpoints");
    if(!cJSON_IsArray(endpoints))return Protocol;
    int count=cJSON_GetArraySize(endpoints);
    if(count<1 || count>static_cast<int>(P4_AOQ_ENDPOINTS_MAX))return Protocol;
    for(int i=0;i<count;++i) {
        const auto item=cJSON_GetArrayItem(endpoints,i);auto &ep=value.endpoints[i];uint32_t port=0;
        if(!copy_string(item,"endpoint",ep.host,sizeof(ep.host)) || !host(ep.host) ||
           !number(item,"port",1,65535,port) || !number(item,"route_index",0,UINT32_MAX,ep.route_index))return Protocol;
        ep.port=static_cast<uint16_t>(port);
    }
    value.endpoint_count=static_cast<size_t>(count);value.kind=kind;
    value.received_at_ms=esp_timer_get_time()/1000;
    *out=value;return Ok;
}
const char *url(p4_aoq_kind_t kind) {
    switch(kind) {
    case P4_AOQ_AUDIO:return "https://maas.qianwenaiapi.com/api/v1/webrtc/realtime?model=" AGENT_AUDIO_MODEL;
    case P4_AOQ_ASR:return "https://maas.qianwenaiapi.com/api/v1/webrtc/inference?model=qwen-audio-3.1-asr-flash-streaming";
    case P4_AOQ_TTS:return "https://maas.qianwenaiapi.com/api/v1/webrtc/inference?model=qwen-audio-3.0-tts-flash";
    case P4_AOQ_OMNI:return "https://maas.qianwenaiapi.com/api/v1/webrtc/realtime?model=qwen3.8-omni-flash-realtime";
    default:return nullptr;
    }
}
int http_status(int status) {
    return status==401?AuthError:status==403?Forbidden:status==429?Rate:
           status>=500?Server:Protocol;
}
bool budget(esp_http_client_handle_t h,int64_t deadline) {
    int64_t left=deadline-esp_timer_get_time()/1000;
    if(left<=0)return false;
    return esp_http_client_set_timeout_ms(h,static_cast<int>(left))==ESP_OK;
}
} // namespace

extern "C" void p4_aoq_allocation_clear(p4_aoq_allocation_t *allocation) {
    if(allocation)wipe(allocation,sizeof(*allocation));
}
extern "C" int p4_aoq_allocate(p4_aoq_kind_t kind,p4_aoq_allocation_t *out,unsigned timeout_ms) {
    if(!out)return Argument;
    p4_aoq_allocation_clear(out);
    const char *endpoint=url(kind);
    if(!endpoint || timeout_ms<1000 || timeout_ms>30000)return Argument;
    Auth auth;
    int error=esp_agent_qianwen_auth(auth.text,sizeof(auth.text));
    if(error)return error;
    if(strncmp(auth.text,"Bearer ",7) || !auth.text[7])return AuthError;
    for(const char *p=auth.text;*p;++p)if(*p=='\r' || *p=='\n')return AuthError;
    Body body;if(!body.data)return Memory;
    esp_http_client_config_t cfg{};
    cfg.url=endpoint;cfg.method=HTTP_METHOD_POST;cfg.timeout_ms=static_cast<int>(timeout_ms);
    cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;
    cfg.buffer_size=1024;cfg.buffer_size_tx=1024;
    Http http;http.handle=esp_http_client_init(&cfg);if(!http.handle)return Memory;
    if(esp_http_client_set_header(http.handle,"Content-Type","application/json")!=ESP_OK ||
       esp_http_client_set_header(http.handle,"Authorization",auth.text)!=ESP_OK ||
       esp_http_client_set_header(http.handle,"x-dashscope-rtc-transport","moq")!=ESP_OK)return Network;
    const int64_t deadline=esp_timer_get_time()/1000+timeout_ms;
    if(esp_http_client_open(http.handle,2)!=ESP_OK)return esp_timer_get_time()/1000>=deadline?Timeout:Network;
    if(!budget(http.handle,deadline))return Timeout;
    if(esp_http_client_write(http.handle,"{}",2)!=2)return Network;
    if(!budget(http.handle,deadline))return Timeout;
    int64_t content_length=esp_http_client_fetch_headers(http.handle);
    if(content_length<0)return esp_timer_get_time()/1000>=deadline?Timeout:Network;
    int status=esp_http_client_get_status_code(http.handle);
    if(status!=200)return http_status(status);
    if(content_length>P4_AOQ_RESPONSE_MAX)return Limit;
    size_t used=0;
    while(true) {
        if(!budget(http.handle,deadline))return Timeout;
        /* One extra byte detects oversized chunked responses without writing
         * beyond the allocation; HTTP's small cached tail is drained as well. */
        size_t room=P4_AOQ_RESPONSE_MAX+1u-used;
        int request=static_cast<int>(room<1024?room:1024);
        int got=esp_http_client_read(http.handle,body.data+used,request);
        if(got<0)return esp_timer_get_time()/1000>=deadline?Timeout:Network;
        used+=static_cast<size_t>(got);
        if(used>P4_AOQ_RESPONSE_MAX)return Limit;
        if(!got) {
            if(!esp_http_client_is_complete_data_received(http.handle))return Protocol;
            break;
        }
    }
    return parse(body.data,used,kind,out);
}
