#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_BUFFER_SIZE 1024
#define OTA_SHA256_LEN 32
#define OTA_SHA256_HEX_LEN (OTA_SHA256_LEN * 2)
#define CMD_BUFFER_SIZE 512
#define USB_JTAG_READ_TIMEOUT_MS 1000
#define USB_JTAG_LINE_POLL_MS 20

void ota_console_init(void);
void ota_console_task(void *arg);
void ota_mark_running_partition_valid(void);
bool ota_running(void);
void usb_flush(void);