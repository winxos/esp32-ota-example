#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_state.h"

void ota_mark_running_partition_valid(void);
esp_err_t ota_perform_from_console(app_state_t *state,
                                   size_t image_size,
                                   const uint8_t expected_digest[OTA_SHA256_LEN]);
