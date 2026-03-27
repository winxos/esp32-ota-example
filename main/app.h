#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    APP_STATE_STOPPED = 0,
    APP_STATE_STARTING,
    APP_STATE_IDLE,
    APP_STATE_BUSY,
    APP_STATE_STOPPING,
} app_state_t;

void app_task(void *arg);

app_state_t app_get_state(void);
const char *app_state_name(app_state_t state);
bool app_is_idle(void);

bool app_on_received(const uint8_t *data, size_t length);

esp_err_t app_start(void);
esp_err_t app_stop(void);
