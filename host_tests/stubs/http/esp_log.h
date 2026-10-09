void test_http_log(const char *,const char *,...);
#define ESP_LOGW(tag,...) test_http_log(tag,__VA_ARGS__)
