#ifndef TEST_HTTP_CLIENT_H
#define TEST_HTTP_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_TIMEOUT=1,ESP_ERR_HTTP_EAGAIN=2,HTTP_METHOD_GET,HTTP_METHOD_POST};
#define ESP_ERR_HTTP_CONNECTING 9
typedef struct test_http *esp_http_client_handle_t;
typedef struct {
    const char *url,*cert_pem;
    int timeout_ms,buffer_size,buffer_size_tx;
    bool disable_auto_redirect,keep_alive_enable,save_client_session;
    esp_err_t (*crt_bundle_attach)(void *);
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t,const char *);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t,int);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t,const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int esp_http_client_get_socket(esp_http_client_handle_t);
int esp_http_client_write(esp_http_client_handle_t,const char *,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
bool esp_http_client_is_persistent_connection(esp_http_client_handle_t);
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t,int *,int *);
#endif
