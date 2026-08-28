#ifndef ONENET_MQTT_H
#define ONENET_MQTT_H

#include "cJSON.h"
#include "esp_err.h"

esp_err_t onenet_mqtt_start(void);
esp_err_t onenet_mqtt_stop(void);
esp_err_t onenet_mqtt_report_properties(const cJSON *params);

#endif
