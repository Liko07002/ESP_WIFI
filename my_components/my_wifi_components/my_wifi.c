#include "my_wifi.h"

void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void wifi_event_handler (void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect ();// 连接 wifi
        printf ("wifi 连接 \n");

    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf ("wifi 重连 \n");
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *) event_data;
        ESP_LOGE ("wifi", "Disconnect reason = % d", disconn->reason);
        esp_wifi_connect ();// 连接 wifi
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        //e = (const ip_event_assigned_ip_to_client_t *) event_data;
        printf ("wifi 连接成功 \n");
    }

}

void wifi_init (void)
{
    ESP_ERROR_CHECK (esp_netif_init ());// 初始化 tcpip 栈
    ESP_ERROR_CHECK (esp_event_loop_create_default ());// 硬件创建事件循环

    ESP_ERROR_CHECK (esp_event_handler_register (WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handler,NULL));// 监听 wifi 事件
    ESP_ERROR_CHECK (esp_event_handler_register (IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handler,NULL));// 监听 ip 事件

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
    ESP_ERROR_CHECK (esp_wifi_init (&wifi_init_config));// 初始化 wifi
    ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_STA));// 设置 wifi 模式为 station

    esp_netif_create_default_wifi_sta ();// 创建默认的 wifi station 模式

    //wifi station 的配置
    wifi_config_t wifi_sta_config =
    {
        .sta =
        {
            .ssid = "123",
            .password = "12345678",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK (esp_wifi_set_config (WIFI_IF_STA,&wifi_sta_config));// 设置 wifi station 的配置

    ESP_ERROR_CHECK (esp_wifi_start ());// 启动 wifi
    esp_wifi_set_ps (WIFI_PS_NONE);// 设置 wifi power save mode 为 none

}




