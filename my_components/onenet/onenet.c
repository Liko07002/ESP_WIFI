#include "onenet.h"

#include "onenet_internal.h"
#include "onenet_mqtt.h"
#include "onenet_ota.h"

static onenet_status_cb_t s_status_callback;
static onenet_property_set_cb_t s_property_callback;
static onenet_ota_notify_cb_t s_ota_callback;
static volatile bool s_connected;
static bool s_started;

esp_err_t onenet_set_status_callback(onenet_status_cb_t callback)
{
    s_status_callback = callback;
    return ESP_OK;
}

esp_err_t onenet_set_property_callback(onenet_property_set_cb_t callback)
{
    s_property_callback = callback;
    return ESP_OK;
}

esp_err_t onenet_set_ota_callback(onenet_ota_notify_cb_t callback)
{
    s_ota_callback = callback;
    return ESP_OK;
}

esp_err_t onenet_start(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = onenet_mqtt_start();
    if (err == ESP_OK) s_started = true;
    return err;
}

esp_err_t onenet_stop(void)
{
    if (!s_started) return ESP_OK;
    esp_err_t err = onenet_mqtt_stop();
    if (err == ESP_OK) {
        s_started = false;
        onenet_internal_mqtt_status_changed(false);
    }
    return err;
}

bool onenet_is_connected(void)
{
    return s_connected;
}

esp_err_t onenet_report_properties(const cJSON *params)
{
    if (params == NULL || !cJSON_IsObject(params)) return ESP_ERR_INVALID_ARG;
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    return onenet_mqtt_report_properties(params);
}

esp_err_t onenet_ota_start(void)
{
    return onenet_ota_component_start();
}

void onenet_internal_mqtt_status_changed(bool connected)
{
    if (s_connected == connected) return;
    s_connected = connected;
    if (connected) onenet_ota_on_mqtt_connected();
    if (s_status_callback != NULL) s_status_callback(connected);
}

esp_err_t onenet_internal_property_received(const cJSON *params)
{
    return s_property_callback != NULL
        ? s_property_callback(params)
        : ESP_ERR_NOT_SUPPORTED;
}

void onenet_internal_ota_notified(void)
{
    if (s_ota_callback != NULL) s_ota_callback();
    else (void)onenet_ota_component_start();
}
