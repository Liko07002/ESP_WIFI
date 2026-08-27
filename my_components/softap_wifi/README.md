# SoftAP Wi-Fi 组件

该组件负责 ESP32 的 STA 自动联网、NVS 凭据保存、断线恢复和 SoftAP 网页配网。
其他模块不需要直接操作 `esp_wifi`。

## 最简用法

```c
#include "softap_wifi_cfg.h"

void app_main(void)
{
    ESP_ERROR_CHECK(softap_wifi_cfg_start());
}
```

不注册状态回调也能正常运行。需要控制联网指示灯时，可以在启动前注册：

```c
static void wifi_status_changed(bool connected)
{
    wifi_led_set(connected);
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        softap_wifi_cfg_set_status_callback(wifi_status_changed));
    ESP_ERROR_CHECK(softap_wifi_cfg_start());
}
```

组件只在联网状态需要用户关注时通知回调：开机连接成功通知 `true`；连接超过静默恢复时间仍失败通知 `false`；静默重连期间不会反复通知。

## 对外接口

- `softap_wifi_cfg_start()`：启动自动联网或配网。
- `softap_wifi_cfg_stop()`：停止并释放组件资源。
- `softap_wifi_cfg_is_connected()`：查询 STA 是否已经取得 IPv4 地址。
- `softap_wifi_cfg_get_sta_ip()`：读取当前 STA IPv4 地址。
- `softap_wifi_cfg_start_provisioning()`：保留旧凭据并开启配网热点。
- `softap_wifi_cfg_clear_credentials()`：清除凭据、断开 STA 并开启配网热点。
- `softap_wifi_cfg_set_status_callback()`：注册或取消可选联网状态回调。

## 配置

用户可修改的宏全部位于 `softap_wifi_config.h`，其中包含详细中文说明。默认行为如下：

- 连接旧 Wi-Fi 最多静默等待 15 秒；
- 超时后启动配网热点，并每 30 秒后台重试旧网络；
- 配网成功后等待网页按钮，60 秒未操作则自动关闭热点；
- 新凭据只有成功取得 IP 后才写入普通 NVS；
- DNS 引导、HTTP 重定向和 DHCP 配网页面地址共同提高手机自动弹出页面的概率。

自动弹窗由手机操作系统最终决定。如果系统没有弹窗，可手动访问默认地址 `http://192.168.100.1/`。
