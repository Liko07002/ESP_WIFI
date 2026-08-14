#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "my_wifi.h"
#include "my_ap_wifi.h"


void app_main(void)
{
    nvs_init();

    //wifi_sta_init();
    //wifi_ap_init();
    //wifi_scan_init();
    //wifi_apsta_init();
    ap_wifi_init();

    while (1)
    {
    vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 5 秒
    printf ("wifi\n");
    };
    //Liko
}