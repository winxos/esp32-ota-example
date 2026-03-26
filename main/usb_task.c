#include "usb_task.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota_service.h"

void usb_console_init(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 4096,
    };

    if (!usb_serial_jtag_is_driver_installed()) {
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    }

    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
}

static void usb_flush(void)
{
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
}

static bool usb_read_line(char *buffer, size_t buffer_size)
{
    size_t length = 0;

    while (true) {
        uint8_t ch = 0;
        int read_count = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(USB_JTAG_LINE_POLL_MS));

        if (read_count < 0) {
            return false;
        }

        if (read_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(USB_JTAG_LINE_POLL_MS));
            continue;
        }

        if (ch == '\n') {
            break;
        }

        if (ch == '\r') {
            continue;
        }

        if (length + 1 < buffer_size) {
            buffer[length++] = (char)ch;
        }
    }

    buffer[length] = '\0';
    return true;
}

static void print_info(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *app_desc = esp_app_get_description();

    printf("INFO project=%s version=%s running=%s boot=%s next=%s secure_version=%" PRIu32 "\n",
           app_desc->project_name,
           app_desc->version,
           running ? running->label : "none",
           configured ? configured->label : "none",
           next ? next->label : "none",
           app_desc->secure_version);
}

static void print_help(void)
{
    puts("OK commands: help ping info echo <text> reboot ota <size> <sha256>");
}

static void trim_line(char *line)
{
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
        line[--len] = '\0';
    }
}

static bool parse_size_arg(const char *text, size_t *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);

    if (text[0] == '\0' || end == NULL || *end != '\0') {
        return false;
    }

    *value = (size_t)parsed;
    return true;
}

static bool parse_sha256_hex(const char *text, uint8_t out_digest[OTA_SHA256_LEN])
{
    if (strlen(text) != OTA_SHA256_HEX_LEN) {
        return false;
    }

    for (size_t index = 0; index < OTA_SHA256_LEN; ++index) {
        char high = (char)tolower((unsigned char)text[index * 2]);
        char low = (char)tolower((unsigned char)text[index * 2 + 1]);
        int high_value;
        int low_value;

        if (high >= '0' && high <= '9') {
            high_value = high - '0';
        } else if (high >= 'a' && high <= 'f') {
            high_value = high - 'a' + 10;
        } else {
            return false;
        }

        if (low >= '0' && low <= '9') {
            low_value = low - '0';
        } else if (low >= 'a' && low <= 'f') {
            low_value = low - 'a' + 10;
        } else {
            return false;
        }

        out_digest[index] = (uint8_t)((high_value << 4) | low_value);
    }

    return true;
}

void usb_task(void *arg)
{
    app_state_t *state = (app_state_t *)arg;
    char line[CMD_BUFFER_SIZE];

    puts("OK cdc_iap_ready");
    print_info();
    print_help();
    usb_flush();

    while (true) {
        if (!usb_read_line(line, sizeof(line))) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }

        char *command = strtok(line, " ");
        if (command == NULL) {
            continue;
        }

        if (strcmp(command, "help") == 0) {
            print_help();
            usb_flush();
            continue;
        }

        if (strcmp(command, "ping") == 0) {
            puts("OK pong");
            usb_flush();
            continue;
        }

        if (strcmp(command, "info") == 0) {
            print_info();
            usb_flush();
            continue;
        }

        if (strcmp(command, "echo") == 0) {
            char *payload = strtok(NULL, "");
            printf("OK %s\n", payload ? payload : "");
            usb_flush();
            continue;
        }

        if (strcmp(command, "reboot") == 0) {
            puts("OK rebooting");
            usb_flush();
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }

        if (strcmp(command, "ota") == 0) {
            char *size_text = strtok(NULL, " ");
            char *sha_text = strtok(NULL, " ");
            size_t image_size = 0;
            uint8_t expected_digest[OTA_SHA256_LEN];
            const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

            if (size_text == NULL || sha_text == NULL ||
                !parse_size_arg(size_text, &image_size) ||
                !parse_sha256_hex(sha_text, expected_digest) ||
                image_size == 0) {
                puts("ERR usage ota <size> <sha256>");
                usb_flush();
                continue;
            }

            if (update_partition == NULL) {
                puts("ERR no_update_partition");
                usb_flush();
                continue;
            }

            state->ota_in_progress = true;
            printf("READY partition=%s size=%u\n", update_partition->label, (unsigned)image_size);
            usb_flush();

            esp_err_t ota_err = ota_perform_from_console(state, image_size, expected_digest);
            if (ota_err == ESP_OK) {
                puts("OK ota_complete rebooting");
                usb_flush();
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }

            state->ota_in_progress = false;
            continue;
        }

        printf("ERR unknown_command %s\n", command);
        usb_flush();
    }
}
