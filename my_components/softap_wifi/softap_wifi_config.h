#ifndef SOFTAP_WIFI_CONFIG_H
#define SOFTAP_WIFI_CONFIG_H

/*
 * SoftAP Wi-Fi 组件统一配置文件
 *
 * 本文件只放置用户可能需要调整的配置。除非正在修改组件内部实现，
 * 否则不建议在其他源文件中直接新增配网相关宏。
 */

/* 配网热点名称。长度必须为 1～32 字节（中文 UTF-8 会占多个字节）。 */
#define SOFTAP_WIFI_AP_SSID                     "ESP32"

/*
 * 配网热点密码。设置为空字符串时热点为开放网络；非空时必须为 8～63 字节。
 * 正式产品不建议使用示例密码，应该修改为设备专用密码。
 */
#define SOFTAP_WIFI_AP_PASSWORD                 "12345678"

/* 设置为 1 时，在热点名后追加 MAC 地址末两字节，例如 ESP32-A1B2。 */
#define SOFTAP_WIFI_AP_APPEND_MAC_SUFFIX        0

/* 配网热点使用的 2.4 GHz 信道，有效范围为 1～13，请遵守设备销售地区法规。 */
#define SOFTAP_WIFI_AP_CHANNEL                  6

/* 最多允许同时连接配网热点的终端数量，有效范围为 1～10。 */
#define SOFTAP_WIFI_AP_MAX_CONNECTIONS          4

/* 配网热点的 IPv4 地址、网关和子网掩码。IP 与网关通常保持一致。 */
#define SOFTAP_WIFI_AP_IP_A                     192
#define SOFTAP_WIFI_AP_IP_B                     168
#define SOFTAP_WIFI_AP_IP_C                     100
#define SOFTAP_WIFI_AP_IP_D                     1
#define SOFTAP_WIFI_AP_NETMASK_A                255
#define SOFTAP_WIFI_AP_NETMASK_B                255
#define SOFTAP_WIFI_AP_NETMASK_C                255
#define SOFTAP_WIFI_AP_NETMASK_D                0

/* 第一次连接Wi-Fi,开机连接旧 Wi-Fi、运行中静默恢复网络的最长等待时间，单位：毫秒。 */
#define SOFTAP_WIFI_CONNECT_TIMEOUT_MS          30000U


/* 静默连接阶段的重试间隔，单位：毫秒。过短会增加功耗和日志数量。 */
#define SOFTAP_WIFI_FAST_RETRY_INTERVAL_MS      1000U

/* 已进入配网模式后，后台尝试旧 Wi-Fi 的间隔，单位：毫秒。 */
#define SOFTAP_WIFI_BACKGROUND_RETRY_MS         30000U

/* 配网成功后等待用户点击关闭热点的时间，单位：毫秒；超时后自动关闭。 */
#define SOFTAP_WIFI_PORTAL_AUTO_CLOSE_MS        60000U

/* 单次扫描最多返回的热点数量。数值越大，扫描结果 JSON 占用的堆内存越多。 */
#define SOFTAP_WIFI_SCAN_MAX_RESULTS            20U

/* WebSocket 单条客户端请求允许的最大字节数。 */
#define SOFTAP_WIFI_WS_MAX_MESSAGE_SIZE         512U

/* HTTP 配网服务器监听端口。使用自动弹出配网页面时建议保持 80。 */
#define SOFTAP_WIFI_HTTP_PORT                   80

/* 设置为 1 时启用 DNS 劫持，以提高 Android、iOS 和 Windows 自动弹窗概率。 */
#define SOFTAP_WIFI_CAPTIVE_DNS_ENABLED         1

/* 管理任务和扫描任务的栈大小（字节）及 FreeRTOS 优先级。 */
#define SOFTAP_WIFI_MANAGER_TASK_STACK          5120U
#define SOFTAP_WIFI_MANAGER_TASK_PRIORITY       4U
#define SOFTAP_WIFI_SCAN_TASK_STACK             4096U
#define SOFTAP_WIFI_SCAN_TASK_PRIORITY          3U
#define SOFTAP_WIFI_DNS_TASK_STACK              3072U
#define SOFTAP_WIFI_DNS_TASK_PRIORITY           3U

/* 保存 Wi-Fi 凭据的 NVS 命名空间和键名，通常无需修改。 */
#define SOFTAP_WIFI_NVS_NAMESPACE               "softap_wifi"
#define SOFTAP_WIFI_NVS_SSID_KEY                "ssid"
#define SOFTAP_WIFI_NVS_PASSWORD_KEY            "password"

#endif
