#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_BUFFER_SIZE 1024
#define OTA_SHA256_LEN 32
#define OTA_SHA256_HEX_LEN (OTA_SHA256_LEN * 2)
#define CMD_BUFFER_SIZE 192
#define USB_JTAG_READ_TIMEOUT_MS 1000
#define USB_JTAG_LINE_POLL_MS 20

typedef struct {
    volatile bool ota_in_progress;
} app_state_t;
