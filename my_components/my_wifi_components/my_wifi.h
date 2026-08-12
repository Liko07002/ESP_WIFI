#ifndef __MY_WIFI_H__
#define __MY_WIFI_H__

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"

void nvs_init(void);
void wifi_sta_init(void);
void wifi_ap_init(void);


#endif

