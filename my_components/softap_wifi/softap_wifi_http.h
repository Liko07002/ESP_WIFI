#ifndef SOFTAP_WIFI_HTTP_H
#define SOFTAP_WIFI_HTTP_H

#include <stddef.h>

#include "esp_err.h"
#include "esp_wifi_types_generic.h"

typedef esp_err_t (*softap_wifi_http_scan_cb_t)(void *ctx);
typedef esp_err_t (*softap_wifi_http_action_cb_t)(void *ctx);
typedef esp_err_t (*softap_wifi_http_credentials_cb_t)(
    const char *ssid,
    const char *password,
    void *ctx);

typedef struct {
    softap_wifi_http_scan_cb_t on_scan;
    softap_wifi_http_credentials_cb_t on_credentials;
    softap_wifi_http_action_cb_t on_state;
    softap_wifi_http_action_cb_t on_close;
    void *ctx;
} softap_wifi_http_callbacks_t;

esp_err_t softap_wifi_http_start(const softap_wifi_http_callbacks_t *callbacks);
esp_err_t softap_wifi_http_stop(void);
esp_err_t softap_wifi_http_send_scan_result(
    const wifi_ap_record_t *records,
    size_t count);
esp_err_t softap_wifi_http_send_status(
    const char *state,
    const char *message,
    const char *ip,
    int reason);

#endif
