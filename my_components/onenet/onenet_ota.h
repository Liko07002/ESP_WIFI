#ifndef ONENET_OTA_H
#define ONENET_OTA_H

#include "esp_err.h"

esp_err_t onenet_ota_component_start(void);
void onenet_ota_on_mqtt_connected(void);

#endif
