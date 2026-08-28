#ifndef ONENET_INTERNAL_H
#define ONENET_INTERNAL_H

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

void onenet_internal_mqtt_status_changed(bool connected);
esp_err_t onenet_internal_property_received(const cJSON *params);
void onenet_internal_ota_notified(void);

#endif
