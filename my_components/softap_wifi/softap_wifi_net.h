#ifndef SOFTAP_WIFI_NET_H
#define SOFTAP_WIFI_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types_generic.h"

typedef void (*softap_wifi_scan_done_cb_t)(const wifi_ap_record_t *records,
    size_t count, esp_err_t result, void *ctx);
typedef void (*softap_wifi_net_status_cb_t)(bool connected, const char *ip,
    uint8_t disconnect_reason, void *ctx);

esp_err_t softap_wifi_net_init(softap_wifi_net_status_cb_t callback, void *ctx);
esp_err_t softap_wifi_net_connect(const char *ssid, const char *password);
esp_err_t softap_wifi_net_reconnect(void);
esp_err_t softap_wifi_net_disconnect(void);
esp_err_t softap_wifi_net_ap_start(void);
esp_err_t softap_wifi_net_ap_stop(void);
esp_err_t softap_wifi_net_scan_start(softap_wifi_scan_done_cb_t callback, void *ctx);
esp_err_t softap_wifi_net_deinit(void);

#endif
