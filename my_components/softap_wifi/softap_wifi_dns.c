#include "softap_wifi_dns.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "softap_wifi_config.h"

#define TAG "softap_wifi_dns"
#define DNS_PORT 53
#define DNS_PACKET_MAX_SIZE 512

static TaskHandle_t s_dns_task;
static int s_dns_socket = -1;
static volatile bool s_running;

static size_t make_dns_response(uint8_t *packet, size_t length)
{
    if (length < 12U || (packet[2] & 0x80U) != 0U) {
        return 0U;
    }

    size_t offset = 12U;
    while (offset < length && packet[offset] != 0U) {
        uint8_t label_length = packet[offset];
        if ((label_length & 0xC0U) != 0U || offset + label_length + 1U > length) {
            return 0U;
        }
        offset += (size_t)label_length + 1U;
    }
    if (offset + 5U > length || length + 16U > DNS_PACKET_MAX_SIZE) {
        return 0U;
    }
    offset += 5U;

    packet[2] = 0x81;
    packet[3] = 0x80;
    packet[6] = 0x00;
    packet[7] = 0x01;
    packet[8] = packet[9] = packet[10] = packet[11] = 0x00;

    uint8_t answer[16] = {
        0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x3C, 0x00, 0x04,
        SOFTAP_WIFI_AP_IP_A, SOFTAP_WIFI_AP_IP_B,
        SOFTAP_WIFI_AP_IP_C, SOFTAP_WIFI_AP_IP_D,
    };
    memcpy(packet + offset, answer, sizeof(answer));
    return offset + sizeof(answer);
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t packet[DNS_PACKET_MAX_SIZE];
    struct sockaddr_in source;

    while (s_running) {
        socklen_t source_length = sizeof(source);
        int received = recvfrom(s_dns_socket, packet, sizeof(packet), 0,
                                (struct sockaddr *)&source, &source_length);
        if (received <= 0) {
            continue;
        }
        size_t response_length = make_dns_response(packet, (size_t)received);
        if (response_length > 0U) {
            (void)sendto(s_dns_socket, packet, response_length, 0,
                         (struct sockaddr *)&source, source_length);
        }
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t softap_wifi_dns_start(void)
{
#if !SOFTAP_WIFI_CAPTIVE_DNS_ENABLED
    return ESP_OK;
#else
    if (s_running) {
        return ESP_OK;
    }
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "创建 DNS 套接字失败");
        return ESP_FAIL;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_dns_socket, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ESP_LOGE(TAG, "绑定 DNS 端口 53 失败");
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_FAIL;
    }

    s_running = true;
    if (xTaskCreatePinnedToCore(dns_task, "softap_dns", SOFTAP_WIFI_DNS_TASK_STACK, NULL,
                    SOFTAP_WIFI_DNS_TASK_PRIORITY, &s_dns_task, SOFTAP_WIFI_DNS_TASK_CORE) != pdPASS) {
        s_running = false;
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "配网页面 DNS 引导服务已启动");
    return ESP_OK;
#endif
}

esp_err_t softap_wifi_dns_stop(void)
{
    s_running = false;
    if (s_dns_socket >= 0) {
        shutdown(s_dns_socket, SHUT_RDWR);
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    for (unsigned i = 0; s_dns_task != NULL && i < 20U; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_dns_task != NULL) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
    }
    ESP_LOGI(TAG, "配网页面 DNS 引导服务已停止");
    return ESP_OK;
}
