#ifndef SOFTAP_WIFI_STORE_H
#define SOFTAP_WIFI_STORE_H

#include <stdbool.h>

#include "esp_err.h"

#define SOFTAP_WIFI_SSID_MAX_LEN      32U
#define SOFTAP_WIFI_PASSWORD_MAX_LEN  63U

typedef struct {
    char ssid[SOFTAP_WIFI_SSID_MAX_LEN + 1U];
    char password[SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U];
} softap_wifi_credentials_t;

esp_err_t softap_wifi_store_load(softap_wifi_credentials_t *credentials, bool *found);
esp_err_t softap_wifi_store_save(const softap_wifi_credentials_t *credentials);
esp_err_t softap_wifi_store_clear(void);

#endif
