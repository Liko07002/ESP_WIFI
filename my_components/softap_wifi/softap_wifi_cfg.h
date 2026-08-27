#ifndef SOFTAP_WIFI_CFG_H
#define SOFTAP_WIFI_CFG_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Wi-Fi 可用状态改变时调用；true 表示 STA 已取得 IPv4 地址。 */
typedef void (*softap_wifi_status_cb_t)(bool connected);

/** 注册可选状态回调。传入 NULL 可取消回调；未注册回调不影响组件运行。 */
esp_err_t softap_wifi_cfg_set_status_callback(softap_wifi_status_cb_t callback);

/** 异步启动 Wi-Fi 管理。组件会自动连接旧网络或启动配网热点。 */
esp_err_t softap_wifi_cfg_start(void);

/** 停止组件，并释放组件创建的 Wi-Fi、HTTP、DNS 和任务资源。 */
esp_err_t softap_wifi_cfg_stop(void);

/** 返回 STA 当前是否已经取得 IPv4 地址。 */
bool softap_wifi_cfg_is_connected(void);

/** 将当前 STA IPv4 地址复制到调用者缓冲区，建议缓冲区至少为 16 字节。 */
esp_err_t softap_wifi_cfg_get_sta_ip(char *out_ip, size_t out_ip_size);

/** 保留旧凭据和现有 STA 连接，同时开启配网热点。 */
esp_err_t softap_wifi_cfg_start_provisioning(void);

/** 清除已保存凭据、断开 STA，并立即开启配网热点。 */
esp_err_t softap_wifi_cfg_clear_credentials(void);

#ifdef __cplusplus
}
#endif

#endif
