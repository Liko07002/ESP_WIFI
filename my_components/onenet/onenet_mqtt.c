#include "onenet_mqtt.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "onenet_config.h"
#include "onenet_internal.h"
#include "onenet_ota.h"
#include "onenet_token.h"

#define TAG "onenet_mqtt"

static esp_mqtt_client_handle_t s_client;
static char s_token[256];
static uint32_t s_message_id;
static char *s_rx_data;
static size_t s_rx_total;
static char s_rx_topic[160];

static esp_err_t publish_json(const char *topic, const cJSON *root)
{
    if (s_client == NULL || topic == NULL || root == NULL) return ESP_ERR_INVALID_STATE;
    char *payload = cJSON_PrintUnformatted(root);
    if (payload == NULL) return ESP_ERR_NO_MEM;
#if ONENET_ENABLE_PAYLOAD_LOG
    ESP_LOGI(TAG, "发布 topic=%s payload=%s", topic, payload);
#endif
    int message_id = esp_mqtt_client_publish(
        s_client, topic, payload, 0, ONENET_MQTT_QOS, 0);
    cJSON_free(payload);
    return message_id >= 0 ? ESP_OK : ESP_FAIL;
}

static void make_topic(char *out, size_t size, const char *suffix)
{
    snprintf(out, size, "$sys/%s/%s/%s",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, suffix);
}

static esp_err_t send_ack(const char *suffix, const char *id,
                          int code, const char *message)
{
    if (id == NULL) return ESP_ERR_INVALID_ARG;
    char topic[160];
    make_topic(topic, sizeof(topic), suffix);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;
    bool ok = cJSON_AddStringToObject(root, "id", id) != NULL &&
              cJSON_AddNumberToObject(root, "code", code) != NULL &&
              cJSON_AddStringToObject(root, "msg", message) != NULL;
    esp_err_t err = ok ? publish_json(topic, root) : ESP_ERR_NO_MEM;
    cJSON_Delete(root);
    return err;
}

static bool topic_is(const char *topic, const char *suffix)
{
    char expected[160];
    make_topic(expected, sizeof(expected), suffix);
    return strcmp(topic, expected) == 0;
}

static void handle_property_set(const cJSON *root)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsString(id) || id->valuestring == NULL || !cJSON_IsObject(params)) {
        ESP_LOGW(TAG, "忽略格式无效的属性设置消息");
        return;
    }
    esp_err_t result = onenet_internal_property_received(params);
    int code = result == ESP_OK ? 200 :
               result == ESP_ERR_NOT_SUPPORTED ? 501 : 400;
    const char *message = result == ESP_OK ? "success" :
                          result == ESP_ERR_NOT_SUPPORTED ? "not handled" : "failed";
    (void)send_ack("thing/property/set_reply", id->valuestring, code, message);
}

static void handle_ota_inform(const cJSON *root)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(id) || id->valuestring == NULL) {
        ESP_LOGW(TAG, "忽略格式无效的 OTA 通知");
        return;
    }
    if (send_ack("ota/inform_reply", id->valuestring, 200, "success") == ESP_OK) {
        onenet_internal_ota_notified();
    } else {
        ESP_LOGW(TAG, "OTA 通知 ACK 发送失败，仍继续通知应用");
        onenet_internal_ota_notified();
    }
}

static void process_message(const char *topic, const char *data, size_t length)
{
#if ONENET_ENABLE_PAYLOAD_LOG
    ESP_LOGI(TAG, "收到 topic=%s payload=%.*s", topic, (int)length, data);
#endif
    cJSON *root = cJSON_ParseWithLength(data, length);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "MQTT JSON 解析失败");
        cJSON_Delete(root);
        return;
    }
    if (topic_is(topic, "thing/property/set")) handle_property_set(root);
    else if (topic_is(topic, "ota/inform")) handle_ota_inform(root);
    cJSON_Delete(root);
}

static void reset_rx(void)
{
    free(s_rx_data);
    s_rx_data = NULL;
    s_rx_total = 0;
    s_rx_topic[0] = '\0';
}

static void handle_data(esp_mqtt_event_handle_t event)
{
    if (event->total_data_len <= 0 ||
        (size_t)event->total_data_len > ONENET_MQTT_RX_MAX_BYTES ||
        event->current_data_offset < 0 || event->data_len < 0 ||
        event->current_data_offset + event->data_len > event->total_data_len) {
        ESP_LOGW(TAG, "MQTT 消息长度无效或超过限制");
        reset_rx();
        return;
    }
    if (event->current_data_offset == 0) {
        reset_rx();
        if (event->topic_len <= 0 || (size_t)event->topic_len >= sizeof(s_rx_topic)) return;
        memcpy(s_rx_topic, event->topic, event->topic_len);
        s_rx_topic[event->topic_len] = '\0';
        s_rx_data = malloc((size_t)event->total_data_len + 1U);
        if (s_rx_data == NULL) return;
        s_rx_total = (size_t)event->total_data_len;
    }
    if (s_rx_data == NULL || s_rx_total != (size_t)event->total_data_len) return;
    memcpy(s_rx_data + event->current_data_offset, event->data, event->data_len);
    if (event->current_data_offset + event->data_len == event->total_data_len) {
        s_rx_data[s_rx_total] = '\0';
        process_message(s_rx_topic, s_rx_data, s_rx_total);
        reset_rx();
    }
}

static esp_err_t subscribe_topics(void)
{
    char topic[160];
    make_topic(topic, sizeof(topic), "thing/property/post/reply");
    if (esp_mqtt_client_subscribe_single(s_client, topic, ONENET_MQTT_QOS) < 0) return ESP_FAIL;
    make_topic(topic, sizeof(topic), "thing/property/set");
    if (esp_mqtt_client_subscribe_single(s_client, topic, ONENET_MQTT_QOS) < 0) return ESP_FAIL;
    make_topic(topic, sizeof(topic), "ota/inform");
    return esp_mqtt_client_subscribe_single(s_client, topic, ONENET_MQTT_QOS) >= 0
        ? ESP_OK : ESP_FAIL;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "OneNET MQTT 已连接");
            if (subscribe_topics() != ESP_OK) ESP_LOGW(TAG, "订阅 OneNET 主题失败");
            onenet_internal_mqtt_status_changed(true);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "OneNET MQTT 已断开，等待自动重连");
            onenet_internal_mqtt_status_changed(false);
            break;
        case MQTT_EVENT_DATA:
            handle_data(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "OneNET MQTT 发生错误");
            break;
        default:
            break;
    }
}

esp_err_t onenet_mqtt_start(void)
{
    if (s_client != NULL) return ESP_OK;
    memset(s_token, 0, sizeof(s_token));
    if (dev_token_generate(s_token, SIG_METHOD_SHA256,
                           ONENET_TOKEN_EXPIRE_TIMESTAMP, ONENET_PRODUCT_ID,
                           ONENET_DEVICE_NAME, ONENET_ACCESS_KEY) != 0) {
        return ESP_FAIL;
    }
    esp_mqtt_client_config_t config = {
        .broker.address.uri = ONENET_MQTT_URI,
        .broker.address.port = ONENET_MQTT_PORT,
        .credentials.client_id = ONENET_DEVICE_NAME,
        .credentials.username = ONENET_PRODUCT_ID,
        .credentials.authentication.password = s_token,
    };
    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err == ESP_OK) err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        memset(s_token, 0, sizeof(s_token));
    }
    return err;
}

esp_err_t onenet_mqtt_stop(void)
{
    if (s_client == NULL) return ESP_OK;
    reset_rx();
    esp_err_t stop_err = esp_mqtt_client_stop(s_client);
    esp_err_t destroy_err = esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    memset(s_token, 0, sizeof(s_token));
    return stop_err != ESP_OK ? stop_err : destroy_err;
}

esp_err_t onenet_mqtt_report_properties(const cJSON *params)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *params_copy = cJSON_Duplicate(params, true);
    if (root == NULL || params_copy == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(params_copy);
        return ESP_ERR_NO_MEM;
    }
    char id[16];
    snprintf(id, sizeof(id), "%lu", (unsigned long)++s_message_id);
    bool ok = cJSON_AddStringToObject(root, "id", id) != NULL &&
              cJSON_AddStringToObject(root, "version", "1.0") != NULL;
    if (ok) cJSON_AddItemToObject(root, "params", params_copy);
    else cJSON_Delete(params_copy);
    char topic[160];
    make_topic(topic, sizeof(topic), "thing/property/post");
    esp_err_t err = ok ? publish_json(topic, root) : ESP_ERR_NO_MEM;
    cJSON_Delete(root);
    return err;
}
