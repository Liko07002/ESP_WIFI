#include "my_wifi.h"

static const char *TAG = "wifi_scan";

static void print_auth_mode(int authmode)
{
    ESP_LOGI(TAG, "认证方式: ");
    switch (authmode)
    {
        case WIFI_AUTH_OPEN:
            ESP_LOGI(TAG, "开放无密码");
            break;
        case WIFI_AUTH_WEP:
            ESP_LOGI(TAG, "WEP");
            break;
        case WIFI_AUTH_WPA_PSK:
            ESP_LOGI(TAG, "WPA1");
            break;
        case WIFI_AUTH_WPA2_PSK:
            ESP_LOGI(TAG, "WPA2");
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            ESP_LOGI(TAG, "WPA/WPA2混合");
            break;
        case WIFI_AUTH_WPA3_PSK:
            ESP_LOGI(TAG, "WPA3");
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            ESP_LOGI(TAG, "WPA2/WPA3混合");
            break;
        case WIFI_AUTH_WAPI_PSK:
            ESP_LOGI(TAG, "WAPI");
            break;
        default:
            ESP_LOGI(TAG, "未知认证");
            break;
    }
}

/**
 * @brief 打印WiFi单播/组播加密套件
 * @param pairwise_cipher 单播加密（设备与路由器之间）
 * @param group_cipher    组播加密（广播数据）
 * @retval 无
 */
static void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    ESP_LOGI(TAG, "单播加密: ");
    switch (pairwise_cipher)
    {
        case WIFI_CIPHER_TYPE_NONE:    ESP_LOGI(TAG, "无加密"); break;
        case WIFI_CIPHER_TYPE_WEP40:   ESP_LOGI(TAG, "WEP40"); break;
        case WIFI_CIPHER_TYPE_WEP104:  ESP_LOGI(TAG, "WEP104"); break;
        case WIFI_CIPHER_TYPE_TKIP:    ESP_LOGI(TAG, "TKIP"); break;
        case WIFI_CIPHER_TYPE_CCMP:    ESP_LOGI(TAG, "CCMP(AES)"); break;
        case WIFI_CIPHER_TYPE_TKIP_CCMP: ESP_LOGI(TAG, "TKIP+CCMP"); break;
        case WIFI_CIPHER_TYPE_AES_CMAC128: ESP_LOGI(TAG, "AES_CMAC"); break;
        default: ESP_LOGI(TAG, "未知加密"); break;
    }

    ESP_LOGI(TAG, "组播加密: ");
    switch (group_cipher)
    {
        case WIFI_CIPHER_TYPE_NONE:    ESP_LOGI(TAG, "无加密"); break;
        case WIFI_CIPHER_TYPE_WEP40:   ESP_LOGI(TAG, "WEP40"); break;
        case WIFI_CIPHER_TYPE_WEP104:  ESP_LOGI(TAG, "WEP104"); break;
        case WIFI_CIPHER_TYPE_TKIP:    ESP_LOGI(TAG, "TKIP"); break;
        case WIFI_CIPHER_TYPE_CCMP:    ESP_LOGI(TAG, "CCMP(AES)"); break;
        default: ESP_LOGI(TAG, "未知加密"); break;
    }
}

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
        //esp_wifi_connect ();// 连接 wifi
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
    /*ap 事件*/
    // 事件：有新设备接入AP热点
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
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

    ESP_ERROR_CHECK (esp_event_handler_register (WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handler,NULL));// 监听 wifi 事件
    ESP_ERROR_CHECK (esp_event_handler_register (IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handler,NULL));// 监听 ip 事件

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
    ESP_ERROR_CHECK (esp_netif_init ());// 初始化 tcpip 栈
    ESP_ERROR_CHECK (esp_event_loop_create_default ());// 硬件创建事件循环

    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap ();// 创建默认的 wifi ap 模式

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));// 初始化 wifi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));// 设置 wifiap 模式

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler, NULL, NULL));

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


void wifi_scan_init(void)
{
    ESP_ERROR_CHECK (esp_netif_init ());// 初始化 tcpip 栈
    ESP_ERROR_CHECK (esp_event_loop_create_default ());// 硬件创建事件循环

    esp_netif_create_default_wifi_sta ();// 创建默认的 wifi station 模式

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));// 初始化 wifi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));// 设置 wifi 模式为 station

    ESP_ERROR_CHECK (esp_wifi_start ());// 启动 wifi

    // ========== 2. 阻塞式扫描周边WiFi ==========
    // 参数1：扫描配置NULL=默认；参数2：true=阻塞，扫描完成才往下执行
    ESP_ERROR_CHECK(esp_wifi_scan_start (NULL,true));// 启动 wifi 扫描

    wifi_ap_record_t ap_info[12] = {0};
    uint16_t ap_max_num = 12;  // 最多读取12个热点
    uint16_t ap_real_num = 0;                      // 实际扫描到的热点总数

    //顺序：先获取热点总数，再获取热点信息数组（顺序不能反，否则内存会提前释放）
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_real_num));// 获取本次扫描总共搜到多少热点
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_max_num, ap_info));// 读取热点信息存入ap_info数组，ap_max_num输入输出参数
    
    for (int i = 0; i < ap_max_num && i < ap_real_num; i++)
    {
        // 串口打印完整热点信息
        ESP_LOGI(TAG, "---------- 热点 %d ----------", i + 1);
        ESP_LOGI(TAG, "WiFi名称(SSID): %s", ap_info[i].ssid);
        ESP_LOGI(TAG, "信号强度(RSSI): %d dBm", ap_info[i].rssi);
        print_auth_mode(ap_info[i].authmode);

        // WEP老旧加密不区分单播/组播，跳过加密打印
        if (ap_info[i].authmode != WIFI_AUTH_WEP)
        {
            print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
        }
        ESP_LOGI(TAG, "信道: %d\n", ap_info[i].primary);
    }
}

void wifi_apsta_init(void)
{
    ESP_ERROR_CHECK (esp_netif_init ());// 初始化 tcpip 栈
    ESP_ERROR_CHECK (esp_event_loop_create_default ());// 硬件创建事件循环

    esp_netif_create_default_wifi_sta ();// 创建默认的 wifi station 模式
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap ();// 创建默认的 wifi ap 模式

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));// 初始化 wifi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));// 设置 wifi 模式为 ap station

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler, NULL, NULL));


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

    ESP_ERROR_CHECK (esp_wifi_start ());// 启动 wifi

    // ========== 2. 阻塞式扫描周边WiFi ==========
    // 参数1：扫描配置NULL=默认；参数2：true=阻塞，扫描完成才往下执行
    ESP_ERROR_CHECK(esp_wifi_scan_start (NULL,true));// 启动 wifi 扫描

    wifi_ap_record_t ap_info[12] = {0};
    uint16_t ap_max_num = 12;  // 最多读取12个热点
    uint16_t ap_real_num = 0;                      // 实际扫描到的热点总数

    //顺序：先获取热点总数，再获取热点信息数组（顺序不能反，否则内存会提前释放）
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_real_num));// 获取本次扫描总共搜到多少热点
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_max_num, ap_info));// 读取热点信息存入ap_info数组，ap_max_num输入输出参数
    
    for (int i = 0; i < ap_max_num && i < ap_real_num; i++)
    {
        // 串口打印完整热点信息
        ESP_LOGI(TAG, "---------- 热点 %d ----------", i + 1);
        ESP_LOGI(TAG, "WiFi名称(SSID): %s", ap_info[i].ssid);
        ESP_LOGI(TAG, "信号强度(RSSI): %d dBm", ap_info[i].rssi);
        print_auth_mode(ap_info[i].authmode);

        // WEP老旧加密不区分单播/组播，跳过加密打印
        if (ap_info[i].authmode != WIFI_AUTH_WEP)
        {
            print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
        }
        ESP_LOGI(TAG, "信道: %d\n", ap_info[i].primary);
    }
}
