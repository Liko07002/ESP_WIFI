#include "softap_wifi_cfg.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "softap_wifi_http.h"
#include "softap_wifi_net.h"

#define TAG "softap_wifi_cfg"

#define SOFTAP_WIFI_SSID_MAX_LEN      32U
#define SOFTAP_WIFI_PASSWORD_MIN_LEN   8U
#define SOFTAP_WIFI_PASSWORD_MAX_LEN  63U
#define SOFTAP_WIFI_EVENT_CONNECT     BIT0

typedef struct {
    char ssid[SOFTAP_WIFI_SSID_MAX_LEN + 1U];
    char password[SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U];
} softap_wifi_credentials_t;

static EventGroupHandle_t s_event_group;
static TaskHandle_t s_manager_task;
static softap_wifi_credentials_t s_credentials;
static bool s_started;

static esp_err_t credentials_store(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strnlen(ssid, SOFTAP_WIFI_SSID_MAX_LEN + 1U);
    const size_t password_len = strnlen(password, SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U);

    if (ssid_len == 0U || ssid_len > SOFTAP_WIFI_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (password_len < SOFTAP_WIFI_PASSWORD_MIN_LEN ||
        password_len > SOFTAP_WIFI_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(s_credentials.ssid, ssid, ssid_len);
    s_credentials.ssid[ssid_len] = '\0';
    memcpy(s_credentials.password, password, password_len);
    s_credentials.password[password_len] = '\0';
    return ESP_OK;
}

static void scan_done(
    const wifi_ap_record_t *records,
    size_t count,
    esp_err_t result,
    void *ctx)
{
    (void)ctx;
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 扫描失败：%s", esp_err_to_name(result));
        return;
    }

    esp_err_t err = softap_wifi_http_send_scan_result(records, count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "发送扫描结果失败：%s", esp_err_to_name(err));
    }
}

static esp_err_t scan_requested(void *ctx)
{
    (void)ctx;
    return softap_wifi_net_scan_start(scan_done, NULL);
}

static esp_err_t credentials_received(
    const char *ssid,
    const char *password,
    void *ctx)
{
    (void)ctx;
    esp_err_t err = credentials_store(ssid, password);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "已收到配网信息：SSID=\"%s\"，密码长度=%u",
             s_credentials.ssid, (unsigned)strlen(s_credentials.password));
    xEventGroupSetBits(s_event_group, SOFTAP_WIFI_EVENT_CONNECT);
    return ESP_OK;
}

static void manager_task(void *arg)
{
    (void)arg;
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(
            s_event_group,
            SOFTAP_WIFI_EVENT_CONNECT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        if ((bits & SOFTAP_WIFI_EVENT_CONNECT) != 0U) {
            /* Let the WebSocket handler return before stopping its server. */
            vTaskDelay(pdMS_TO_TICKS(100));
            (void)softap_wifi_http_stop();

            esp_err_t err = softap_wifi_net_connect(
                s_credentials.ssid,
                s_credentials.password);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "启动 STA 连接失败：%s", esp_err_to_name(err));
            }
        }
    }
}

static esp_err_t component_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "擦除 NVS 失败");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t softap_wifi_cfg_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(component_nvs_init(), TAG, "初始化 NVS 失败");

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(manager_task, "softap_cfg", 4096, NULL, 3, &s_manager_task) != pdPASS) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = softap_wifi_net_init();
    if (err != ESP_OK) {
        goto fail;
    }

    const softap_wifi_http_callbacks_t callbacks = {
        .on_scan = scan_requested,
        .on_credentials = credentials_received,
        .ctx = NULL,
    };
    err = softap_wifi_http_start(&callbacks);
    if (err != ESP_OK) {
        (void)softap_wifi_net_deinit();
        goto fail;
    }

    s_started = true;
    ESP_LOGI(TAG, "SoftAP 网页配网已启动");
    return ESP_OK;

fail:
    vTaskDelete(s_manager_task);
    s_manager_task = NULL;
    vEventGroupDelete(s_event_group);
    s_event_group = NULL;
    return err;
}

esp_err_t softap_wifi_cfg_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    esp_err_t http_err = softap_wifi_http_stop();
    esp_err_t net_err = softap_wifi_net_deinit();

    if (s_manager_task != NULL) {
        vTaskDelete(s_manager_task);
        s_manager_task = NULL;
    }
    if (s_event_group != NULL) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
    }

    memset(&s_credentials, 0, sizeof(s_credentials));
    s_started = false;
    return (http_err != ESP_OK) ? http_err : net_err;
}
