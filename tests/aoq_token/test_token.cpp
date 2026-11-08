#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "fixtures/allocation.hpp"
// Compile the actual implementation, including its private parser, in this TU.
// No production source is copied or replaced by a test implementation.
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_token.cpp"

static std::string response;
static int response_status=200,auth_result=0,cleanup_count=0,open_count=0;
static bool chunked=false,incomplete=false,fail_open=false;
static int64_t now_us=1000000,open_delay_us=0;
struct FakeHttp {std::vector<char> auth;size_t offset=0;std::string url;};
esp_err_t esp_crt_bundle_attach(void *) {return ESP_OK;}
int64_t esp_timer_get_time(void) {return now_us;}
extern "C" int esp_agent_qianwen_auth(char *out,size_t cap) {
    assert(cap>32);strcpy(out,"Bearer synthetic-test-key");return auth_result;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    assert(c->disable_auto_redirect && c->crt_bundle_attach==esp_crt_bundle_attach);
    assert(c->method==HTTP_METHOD_POST && c->buffer_size==1024 && c->buffer_size_tx==1024);
    assert(c->timeout_ms>=1000 && c->timeout_ms<=30000);
    assert(std::string(c->url).rfind("https://maas.qianwenaiapi.com/",0)==0);
    auto h=new FakeHttp;h->url=c->url;return h;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) {++cleanup_count;assert(h->auth.empty());delete h;return 0;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value) {
    if(!strcmp(key,"Authorization"))h->auth.assign(value,value+strlen(value)+1);
    else if(!strcmp(key,"Content-Type"))assert(!strcmp(value,"application/json"));
    else {assert(!strcmp(key,"x-dashscope-rtc-transport"));assert(!strcmp(value,"moq"));}
    return 0;
}
esp_err_t esp_http_client_get_header(esp_http_client_handle_t h,const char *,char **value) {*value=h->auth.empty()?nullptr:h->auth.data();return 0;}
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t h,const char *) {for(char c:h->auth)assert(c==0);h->auth.clear();return 0;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int timeout) {assert(timeout>0 && timeout<=30000);return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t,int length) {assert(length==2);++open_count;now_us+=open_delay_us;return fail_open?-1:0;}
int esp_http_client_write(esp_http_client_handle_t,const char *p,int n) {assert(n==2 && !memcmp(p,"{}",2));return n;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t) {return chunked?0:response.size();}
int esp_http_client_get_status_code(esp_http_client_handle_t) {return response_status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int size) {
    size_t n=std::min(static_cast<size_t>(size),response.size()-h->offset);
    memcpy(out,response.data()+h->offset,n);h->offset+=n;return static_cast<int>(n);
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) {return !incomplete && h->offset==response.size();}

std::string valid(const std::string &pin=std::string(64,'a')) {
    std::string body=kSyntheticAllocation;
    const auto at=body.find("PIN_HEX");assert(at!=body.npos);
    body.replace(at,7,pin);return body;
}
std::string replace(std::string s,const std::string &from,const std::string &to) {auto at=s.find(from);assert(at!=s.npos);s.replace(at,from.size(),to);return s;}
void zero(const p4_aoq_allocation_t &out) {for(size_t i=0;i<sizeof(out);++i)assert(reinterpret_cast<const unsigned char *>(&out)[i]==0);}
int main() {
    unsigned cases=0;
    auto rejects=[&](const std::string &s) {p4_aoq_allocation_t out;memset(&out,0x5a,sizeof(out));assert(parse(s.data(),s.size(),P4_AOQ_AUDIO,&out)!=0);zero(out);++cases;};
    p4_aoq_allocation_t out{};
    for(auto pin:{std::string(64,'a'),std::string("sha256/")+std::string(64,'A')}) {
        auto body=valid(pin);assert(parse(body.data(),body.size(),P4_AOQ_AUDIO,&out)==0);assert(out.endpoint_count==1 && out.endpoints[0].port==8443 && out.fingerprint_sha256[0]==0xaa);p4_aoq_allocation_clear(&out);zero(out);++cases;
    }
    rejects(valid(std::string(63,'a')));rejects(valid(std::string(65,'a')));rejects(valid(std::string(64,'g')));
    rejects(valid("sha256/"+std::string(43,'a')+"="));
    rejects(replace(valid(),"\"sid\":\"synthetic-sid\"","\"sid\":\"a\",\"sid\":\"b\""));
    rejects(replace(valid(),"synthetic-token",std::string(1024,'x')));
    rejects(replace(valid(),"synthetic-sid",std::string(256,'x')));
    rejects(replace(valid(),"synthetic-token","x\\u0000ignored"));
    rejects(replace(valid(),"synthetic-token","x\\nignored"));
    rejects(replace(valid(),"relay.example.invalid","https://relay.example.invalid"));
    rejects(replace(valid(),"relay.example.invalid","a@b"));
    rejects(replace(valid(),"8443","65536"));rejects(replace(valid(),"8443","0"));rejects(replace(valid(),"8443","1.5"));
    rejects(replace(valid(),"\"route_index\":0","\"route_index\":-1"));
    rejects(replace(valid(),"\"sidExpiresInSecs\":60","\"sidExpiresInSecs\":0"));
    rejects(replace(valid(),"\"sidExpiresInSecs\":60","\"sidExpiresInSecs\":1e999"));
    rejects(valid()+"{}");rejects(std::string(16385,' '));rejects(std::string(9,'[')+std::string(9,']'));
    auto nul=valid();nul.insert(3,1,'\0');rejects(nul);
    auto ep="{\"endpoint\":\"relay.example.invalid\",\"port\":8443,\"route_index\":0}";
    rejects(replace(valid(),ep,std::string(ep)+","+ep+","+ep+","+ep+","+ep));
    for(auto kind:{P4_AOQ_AUDIO,P4_AOQ_ASR,P4_AOQ_TTS,P4_AOQ_OMNI}) {response=valid();assert(p4_aoq_allocate(kind,&out,5000)==0);assert(out.kind==kind);p4_aoq_allocation_clear(&out);zero(out);++cases;}
    assert(!std::strcmp(url(P4_AOQ_OMNI),"https://maas.qianwenaiapi.com/api/v1/webrtc/realtime?model=qwen3.8-omni-flash-realtime"));
    int opened=open_count;assert(p4_aoq_allocate(static_cast<p4_aoq_kind_t>(99),&out,5000)==Argument);zero(out);assert(open_count==opened);++cases;
    for(unsigned ms:{0u,999u,30001u}) {assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,ms)==Argument);zero(out);++cases;}
    for(auto pair:{std::pair<int,int>{401,AuthError},{403,Forbidden},{429,Rate},{500,Server},{302,Protocol},{400,Protocol}}) {response_status=pair.first;assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,5000)==pair.second);zero(out);++cases;}
    response_status=200;response.assign(16385,' ');
    for(bool streamed:{false,true}) {chunked=streamed;assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,5000)==Limit);zero(out);++cases;}
    chunked=false;response=valid();incomplete=true;assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,5000)==Protocol);zero(out);incomplete=false;++cases;
    auth_result=2;opened=open_count;assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,5000)==2);zero(out);assert(open_count==opened);auth_result=0;++cases;
    open_delay_us=6000000;assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,5000)==Timeout);zero(out);open_delay_us=0;++cases;
    fail_open=true;assert(p4_aoq_allocate(P4_AOQ_AUDIO,&out,5000)==Network);zero(out);++cases;
    assert(cleanup_count==open_count);
    assert(cases==44);
    std::cout << "PASS " << cases << " synthetic parser/HTTP/cleanup cases; no network or credentials used\n";
}
