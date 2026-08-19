#include "softap_wifi_http.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#define TAG "softap_wifi_http"

#define HTML_PATH              "/spiffs/softap_wifi.html"
#define PICO_CSS_PATH          "/spiffs/pico.min.css"
#define HTML_PARTITION_LABEL   "html"
#define WS_MAX_MESSAGE_SIZE    512U

static httpd_handle_t s_server;
static int s_client_fd = -1;
static char *s_index_html;
static bool s_spiffs_mounted;
static softap_wifi_http_callbacks_t s_callbacks;

static esp_err_t ws_send_text(const char *data, size_t len)
{
    if (data == NULL || s_server == NULL || s_client_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (httpd_ws_get_fd_info(s_server, s_client_fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        s_client_fd = -1;
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = len,
    };
    return httpd_ws_send_data(s_server, s_client_fd, &frame);
}

static esp_err_t load_html(void)
{
    const esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = HTML_PARTITION_LABEL,
        .max_files = 2,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err != ESP_OK) {
        return err;
    }
    s_spiffs_mounted = true;

    struct stat st;
    if (stat(HTML_PATH, &st) != 0 || st.st_size <= 0) {
        return ESP_ERR_NOT_FOUND;
    }

    FILE *file = fopen(HTML_PATH, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    s_index_html = calloc(1, (size_t)st.st_size + 1U);
    if (s_index_html == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    const size_t bytes_read = fread(s_index_html, 1U, (size_t)st.st_size, file);
    fclose(file);
    if (bytes_read != (size_t)st.st_size) {
        free(s_index_html);
        s_index_html = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t process_ws_message(const uint8_t *payload, size_t len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)payload, len);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_INVALID_ARG;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");

    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(type->valuestring, "scan_start") == 0) {
        result = (s_callbacks.on_scan != NULL)
            ? s_callbacks.on_scan(s_callbacks.ctx)
            : ESP_ERR_NOT_SUPPORTED;
    } else if (strcmp(type->valuestring, "get_state") == 0) {
        result = (s_callbacks.on_state != NULL)
            ? s_callbacks.on_state(s_callbacks.ctx)
            : ESP_ERR_NOT_SUPPORTED;
    } else if (strcmp(type->valuestring, "close_provisioning") == 0) {
        result = (s_callbacks.on_close != NULL)
            ? s_callbacks.on_close(s_callbacks.ctx)
            : ESP_ERR_NOT_SUPPORTED;
    } else if (strcmp(type->valuestring, "configure") == 0 &&
               cJSON_IsString(ssid) && ssid->valuestring != NULL &&
               cJSON_IsString(password) && password->valuestring != NULL) {
        result = (s_callbacks.on_credentials != NULL)
            ? s_callbacks.on_credentials(
                  ssid->valuestring,
                  password->valuestring,
                  s_callbacks.ctx)
            : ESP_ERR_NOT_SUPPORTED;
    }

    cJSON_Delete(root);
    return result;
}

static void send_request_error(httpd_req_t *req, esp_err_t error)
{
    char json[192];
    int len = snprintf(
        json,
        sizeof(json),
        "{\"type\":\"provision_status\",\"state\":\"request_error\","
        "\"message\":\"请求无效或设备正忙，请稍后重试\",\"reason\":%d}",
        error);
    if (len <= 0 || (size_t)len >= sizeof(json)) {
        return;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = (size_t)len,
    };
    (void)httpd_ws_send_frame(req, &frame);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    const int fd = httpd_req_to_sockfd(req);
    if (httpd_ws_get_fd_info(req->handle, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        return ESP_FAIL;
    }
    s_client_fd = fd;

    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len == 0U) {
        return ESP_OK;
    }
    if (frame.len > WS_MAX_MESSAGE_SIZE) {
        ESP_LOGW(TAG, "WebSocket 消息过长：%u 字节", (unsigned)frame.len);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *payload = calloc(1, frame.len + 1U);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK && frame.type == HTTPD_WS_TYPE_TEXT) {
        err = process_ws_message(payload, frame.len);
    }
    free(payload);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket 请求已拒绝：%s", esp_err_to_name(err));
        send_request_error(req, err);
    }
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    if (s_index_html == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTML unavailable");
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_file(
    httpd_req_t *req,
    const char *path,
    const char *content_type)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "文件不存在");
    }

    httpd_resp_set_type(req, content_type);
    char buffer[1024];
    esp_err_t result = ESP_OK;
    size_t read_len;
    while ((read_len = fread(buffer, 1U, sizeof(buffer), file)) > 0U) {
        result = httpd_resp_send_chunk(req, buffer, read_len);
        if (result != ESP_OK) {
            break;
        }
    }
    fclose(file);

    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(req, NULL, 0);
    }
    return result;
}

static esp_err_t pico_css_handler(httpd_req_t *req)
{
    return send_file(req, PICO_CSS_PATH, "text/css; charset=utf-8");
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t softap_wifi_http_start(const softap_wifi_http_callbacks_t *callbacks)
{
    if (callbacks == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = load_html();
    if (err != ESP_OK) {
        (void)softap_wifi_http_stop();
        return err;
    }
    s_callbacks = *callbacks;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        (void)softap_wifi_http_stop();
        return err;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    const httpd_uri_t favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
    };
    const httpd_uri_t pico_css = {
        .uri = "/pico.min.css",
        .method = HTTP_GET,
        .handler = pico_css_handler,
    };
    const httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };

    if ((err = httpd_register_uri_handler(s_server, &root)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &favicon)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &pico_css)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &ws)) != ESP_OK) {
        (void)softap_wifi_http_stop();
        return err;
    }

    ESP_LOGI(TAG, "HTTP 配网服务器已启动");
    return ESP_OK;
}

esp_err_t softap_wifi_http_send_scan_result(
    const wifi_ap_record_t *records,
    size_t count)
{
    if (records == NULL && count != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *list = NULL;
    if (root != NULL) {
        cJSON_AddStringToObject(root, "type", "scan_result");
        list = cJSON_AddArrayToObject(root, "networks");
    }
    if (root == NULL || list == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL ||
            cJSON_AddStringToObject(item, "ssid", (const char *)records[i].ssid) == NULL ||
            cJSON_AddNumberToObject(item, "rssi", records[i].rssi) == NULL ||
            cJSON_AddBoolToObject(item, "encrypted", records[i].authmode != WIFI_AUTH_OPEN) == NULL) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToArray(list, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ws_send_text(json, strlen(json));
    cJSON_free(json);
    return err;
}

esp_err_t softap_wifi_http_send_status(
    const char *state,
    const char *message,
    const char *ip,
    int reason)
{
    if (state == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", "provision_status") == NULL ||
        cJSON_AddStringToObject(root, "state", state) == NULL ||
        cJSON_AddStringToObject(root, "message", message) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    if (ip != NULL && cJSON_AddStringToObject(root, "ip", ip) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    if (reason != 0 && cJSON_AddNumberToObject(root, "reason", reason) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ws_send_text(json, strlen(json));
    cJSON_free(json);
    return err;
}

esp_err_t softap_wifi_http_stop(void)
{
    esp_err_t result = ESP_OK;
    if (s_server != NULL) {
        result = httpd_stop(s_server);
        s_server = NULL;
    }
    s_client_fd = -1;
    memset(&s_callbacks, 0, sizeof(s_callbacks));

    free(s_index_html);
    s_index_html = NULL;
    if (s_spiffs_mounted) {
        esp_err_t err = esp_vfs_spiffs_unregister(HTML_PARTITION_LABEL);
        if (result == ESP_OK) {
            result = err;
        }
        s_spiffs_mounted = false;
    }
    return result;
}
