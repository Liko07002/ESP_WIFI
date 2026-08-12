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

static void wifi_sta_event_handler (void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
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
        vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 5 秒
        esp_wifi_connect ();// 连接 wifi
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        //e = (const ip_event_assigned_ip_to_client_t *) event_data;
        printf ("wifi 连接成功 \n");
    }

}

static void wifi_ap_event_handler (void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 事件：有新设备接入AP热点
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        //wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        // MACSTR / MAC2STR 是ESP内置宏，格式化打印MAC地址
        //ESP_LOGI("wifi_ap", "设备接入 MAC:"MACSTR", 接入ID=%d",MAC2STR(event->mac), event->aid);
        printf("设备接入\n");
    } 
    // 事件：设备断开AP热点
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        //wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        //ESP_LOGI(TAG, "设备断开 MAC:"MACSTR", 接入ID=%d",MAC2STR(event->mac), event->aid);
        printf("设备断开\n");
    }
}

void wifi_sta_init (void)
{
    ESP_ERROR_CHECK (esp_netif_init ());// 初始化 tcpip 栈
    ESP_ERROR_CHECK (esp_event_loop_create_default ());// 硬件创建事件循环
    esp_netif_create_default_wifi_sta ();// 创建默认的 wifi station 模式


    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
    ESP_ERROR_CHECK (esp_wifi_init (&wifi_init_config));// 初始化 wifi
    ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_STA));// 设置 wifi 模式为 station

    ESP_ERROR_CHECK (esp_event_handler_register (WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_sta_event_handler,NULL));// 监听 wifi 事件
    ESP_ERROR_CHECK (esp_event_handler_register (IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_sta_event_handler,NULL));// 监听 ip 事件

    //wifi station 的配置
    wifi_config_t wifi_sta_config =
    {
        .sta =
        {
            .ssid = "123",
            .password = "12345678",
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK (esp_wifi_set_config (WIFI_IF_STA,&wifi_sta_config));// 设置 wifi station 的配置

    ESP_ERROR_CHECK (esp_wifi_start ());// 启动 wifi
    //esp_wifi_set_ps (WIFI_PS_NONE);// 设置 wifi power save mode 为 none

}

void wifi_ap_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init ());// 初始化 tcpip 栈
    ESP_ERROR_CHECK(esp_event_loop_create_default ());// 硬件创建事件循环
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap ();// 创建默认的 wifi ap 模式

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));// 初始化 wifi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));// 设置 wifiap 模式

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_ap_event_handler, NULL, NULL));

    //wifi ap 的配置
    wifi_config_t wifi_ap_config =
    {
        .ap =
        {
            .ssid = "esp_wifi",
            .ssid_len = strlen("esp_wifi"),
            .password = "12345678",
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = 6,
            .max_connection = 4,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&wifi_ap_config));// 设置 wifi ap 的配置
    
    //如果是AP模式，则需要设置如下网络层信息
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192,168,100,1);    //本地的IP地址
    IP4_ADDR(&ipInfo.gw, 192,168,100,1);    //网关IP地址
    IP4_ADDR(&ipInfo.netmask, 255,255,255,0);   //子网掩码
    esp_netif_dhcps_stop(esp_netif_ap);        //设置IP地址前需要停用DHCP服务
    esp_netif_set_ip_info(esp_netif_ap, &ipInfo);    //设置IP地址
    esp_netif_dhcps_start(esp_netif_ap);        //重新启动DHCP服务

    ESP_ERROR_CHECK(esp_wifi_start ());// 启动 wifi

}


