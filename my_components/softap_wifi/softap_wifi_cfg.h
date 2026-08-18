#ifndef SOFTAP_WIFI_CFG_H
#define SOFTAP_WIFI_CFG_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start SoftAP web provisioning. Initializes NVS, Wi-Fi, HTTP and WebSocket. */
esp_err_t softap_wifi_cfg_start(void);

/** Stop provisioning and release resources owned by this component. */
esp_err_t softap_wifi_cfg_stop(void);

#ifdef __cplusplus
}
#endif

#endif
