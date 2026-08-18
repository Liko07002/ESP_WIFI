#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "my_wifi.h"
//#include "my_ap_wifi.h"
#include "softap_wifi_cfg.h"


void app_main(void)
{
    nvs_init();

    //wifi_sta_init();
    //wifi_ap_init();
    //wifi_scan_init();
    //wifi_apsta_init();
    //softap_wifi_net_init();
    softap_wifi_cfg_start();
    
    while (1)
    {
    vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 5 秒
    printf ("wifi\n");
    };
    //Liko
}