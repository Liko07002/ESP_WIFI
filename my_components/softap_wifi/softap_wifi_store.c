#include "softap_wifi_store.h"

#include <string.h>

#include "nvs.h"

#include "softap_wifi_config.h"

esp_err_t softap_wifi_store_load(softap_wifi_credentials_t *credentials, bool *found)
{
    if (credentials == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(credentials, 0, sizeof(*credentials));
    *found = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SOFTAP_WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_size = sizeof(credentials->ssid);
    size_t password_size = sizeof(credentials->password);
    err = nvs_get_str(handle, SOFTAP_WIFI_NVS_SSID_KEY, credentials->ssid, &ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, SOFTAP_WIFI_NVS_PASSWORD_KEY,
                          credentials->password, &password_size);
    }
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH) {
        memset(credentials, 0, sizeof(*credentials));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        memset(credentials, 0, sizeof(*credentials));
        return err;
    }

    *found = credentials->ssid[0] != '\0';
    return ESP_OK;
}

esp_err_t softap_wifi_store_save(const softap_wifi_credentials_t *credentials)
{
    if (credentials == NULL || credentials->ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SOFTAP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, SOFTAP_WIFI_NVS_SSID_KEY, credentials->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, SOFTAP_WIFI_NVS_PASSWORD_KEY, credentials->password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t softap_wifi_store_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SOFTAP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
