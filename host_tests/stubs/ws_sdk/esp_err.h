#ifndef TEST_WS_SDK_ERR_H
#define TEST_WS_SDK_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_STATIC_ANALYZER_CHECK(condition, result) do { if(condition)return result; } while(0)
#endif
