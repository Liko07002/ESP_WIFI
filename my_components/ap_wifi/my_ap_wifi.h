#ifndef __MY_AP_WIFI_H__
#define __MY_AP_WIFI_H__    

#include <sys/stat.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"

// 包含spiffs头文件
#include "esp_spiffs.h"
#include "cJSON.h"
#include "esp_http_server.h"

void ap_wifi_init (void);

#endif

