#ifndef SOFTAP_WIFI_CFG_H
#define SOFTAP_WIFI_CFG_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start SoftAP web provisioning. Initializes NVS, Wi-Fi, HTTP and WebSocket. */
esp_err_t softap_wifi_cfg_start(void);

/** Stop provisioning and release resources owned by this component. */
esp_err_t softap_wifi_cfg_stop(void);

/**
 * @brief 获取配网成功后的 STA IPv4 地址。
 *
 * @param out_ip      接收 IPv4 字符串的缓冲区，例如 "192.168.1.100"。
 * @param out_ip_size 缓冲区大小，建议至少为 16 字节。
 * @return
 *      - ESP_OK：已经连接并成功取得 IP。
 *      - ESP_ERR_INVALID_STATE：尚未配网成功或尚未取得 IP。
 *      - ESP_ERR_INVALID_ARG：输出参数无效。
 *      - ESP_ERR_INVALID_SIZE：输出缓冲区太小。
 */
esp_err_t softap_wifi_cfg_get_sta_ip(void);//(char *out_ip, size_t out_ip_size);

#ifdef __cplusplus
}
#endif

#endif
