#ifndef SOFTAP_WIFI_NET_H
#define SOFTAP_WIFI_NET_H

#include <stddef.h>

#include "esp_err.h"
#include "esp_wifi_types_generic.h"

typedef void (*softap_wifi_scan_done_cb_t)(
    const wifi_ap_record_t *records,
    size_t count,
    esp_err_t result,
    void *ctx);

esp_err_t softap_wifi_net_init(void);
esp_err_t softap_wifi_net_connect(const char *ssid, const char *password);
esp_err_t softap_wifi_net_scan_start(softap_wifi_scan_done_cb_t callback, void *ctx);
esp_err_t softap_wifi_net_deinit(void);

#endif
