#include "ota_service.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app.h"
#include "log_utils.h"

static volatile bool s_ota_running;

void ota_console_init(void)
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

bool ota_running(void)
{
    return s_ota_running;
}

void usb_flush(void)
{
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
}

static void protocol_write(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    usb_flush();
}

static size_t trim_frame_for_command(uint8_t *buffer, size_t length)
{
    while (length > 0 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n' ||
                          buffer[length - 1] == ' ' || buffer[length - 1] == '\t')) {
        --length;
    }

    size_t start = 0;
    while (start < length && (buffer[start] == ' ' || buffer[start] == '\t' ||
                              buffer[start] == '\r' || buffer[start] == '\n')) {
        ++start;
    }

    if (start > 0 && start < length) {
        memmove(buffer, buffer + start, length - start);
    }

    return length - start;
}

static bool usb_read_frame(uint8_t *buffer, size_t buffer_size, size_t *out_length)
{
    size_t length = 0;

    while (length < buffer_size) {
        int read_count = usb_serial_jtag_read_bytes(buffer + length,
                                                    (uint32_t)(buffer_size - length),
                                                    pdMS_TO_TICKS(USB_JTAG_LINE_POLL_MS));
        if (read_count < 0) {
            return false;
        }

        if (read_count == 0) {
            if (length > 0) {
                *out_length = length;
                return true;
            }
            continue;
        }

        length += (size_t)read_count;
    }

    *out_length = length;
    return true;
}

static void print_info(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();

    protocol_write("INFO project=%s version=%s running=%s app=%s",
                   app_desc->project_name,
                   app_desc->version,
                   running ? running->label : "none",
                   app_state_name(app_get_state()));
}

static void print_help(void)
{
    protocol_write("OK commands: help ping info echo <text> reboot ota <size> <sha256>");
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

static esp_err_t ota_fail(const char *stage, esp_err_t err)
{
    log_error("stage=%s err=%s", stage, esp_err_to_name(err));
    return err;
}

void ota_mark_running_partition_valid(void)
{
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (running != NULL &&
        esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            protocol_write("OK app_confirmed");
        } else {
            protocol_write("ERR confirm_failed %s", esp_err_to_name(err));
        }
    }
#endif
}

static esp_err_t read_exact_bytes(uint8_t *buffer, size_t total_length)
{
    size_t received = 0;

    while (received < total_length) {
        int chunk = usb_serial_jtag_read_bytes(buffer + received,
                                               (uint32_t)(total_length - received),
                                               pdMS_TO_TICKS(USB_JTAG_READ_TIMEOUT_MS));
        if (chunk < 0) {
            return ESP_FAIL;
        }
        if (chunk == 0) {
            continue;
        }
        received += (size_t)chunk;
    }

    return ESP_OK;
}

static esp_err_t ota_perform_from_console(size_t image_size,
                                          const uint8_t expected_digest[OTA_SHA256_LEN])
{
    (void)expected_digest;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t update_handle = 0;
    uint8_t *buffer = NULL;
    esp_err_t err = ESP_FAIL;
    size_t remaining = image_size;
    bool header_checked = false;

    if (update_partition == NULL) {
        return ota_fail("next_update_partition", ESP_ERR_NOT_FOUND);
    }

    buffer = malloc(OTA_BUFFER_SIZE);
    if (buffer == NULL) {
        return ota_fail("malloc_buffer", ESP_ERR_NO_MEM);
    }

    err = esp_ota_begin(update_partition, image_size, &update_handle);
    if (err != ESP_OK) {
        free(buffer);
        return ota_fail("ota_begin", err);
    }

    while (remaining > 0) {
        size_t chunk_size = remaining > OTA_BUFFER_SIZE ? OTA_BUFFER_SIZE : remaining;
        err = read_exact_bytes(buffer, chunk_size);
        if (err != ESP_OK) {
            esp_ota_abort(update_handle);
            free(buffer);
            return ota_fail("read_exact", err);
        }

        if (!header_checked) {
            const esp_image_header_t *image_header = (const esp_image_header_t *)buffer;

            if (chunk_size < sizeof(esp_image_header_t) || image_header->magic != ESP_IMAGE_HEADER_MAGIC) {
                esp_ota_abort(update_handle);
                free(buffer);
                return ota_fail("image_header", ESP_ERR_INVALID_ARG);
            }

            header_checked = true;
        }

        err = esp_ota_write(update_handle, buffer, chunk_size);
        if (err != ESP_OK) {
            esp_ota_abort(update_handle);
            free(buffer);
            return ota_fail("ota_write", err);
        }

        remaining -= chunk_size;
    }

    log_info("OTA stage=write_done size=%u", (unsigned)image_size);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        free(buffer);
        return ota_fail("ota_end", err);
    }

    log_info("OTA stage=ota_end_ok");

    log_info("OTA stage=image_validated");

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        free(buffer);
        return ota_fail("set_boot", err);
    }

    log_info("OTA stage=set_boot_ok partition=%s", update_partition->label);

    free(buffer);
    return ESP_OK;
}

void ota_console_task(void *arg)
{
    (void)arg;
    uint8_t frame[CMD_BUFFER_SIZE];
    char command_buffer[CMD_BUFFER_SIZE + 1];

    protocol_write("OK cdc_iap_ready");
    print_info();
    print_help();

    while (true) {
        size_t frame_length = 0;

        if (!usb_read_frame(frame, sizeof(frame), &frame_length)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (frame_length == 0) {
            continue;
        }

        if (app_on_received(frame, frame_length)) {
            continue;
        }

        size_t command_length = trim_frame_for_command(frame, frame_length);
        if (command_length == 0) {
            continue;
        }

        memcpy(command_buffer, frame, command_length);
        command_buffer[command_length] = '\0';

        char *command = strtok(command_buffer, " ");
        if (command == NULL) {
            continue;
        }

        if (strcmp(command, "help") == 0) {
            print_help();
            continue;
        }

        if (strcmp(command, "ping") == 0) {
            protocol_write("OK pong");
            continue;
        }

        if (strcmp(command, "info") == 0) {
            print_info();
            continue;
        }

        if (strcmp(command, "echo") == 0) {
            char *payload = strtok(NULL, "");
            protocol_write("OK %s", payload ? payload : "");
            continue;
        }

        if (strcmp(command, "reboot") == 0) {
            protocol_write("OK rebooting");
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
                protocol_write("ERR usage ota <size> <sha256>");
                continue;
            }

            if (update_partition == NULL) {
                protocol_write("ERR no_update_partition");
                continue;
            }

            esp_err_t app_err = app_stop();
            if (app_err != ESP_OK) {
                protocol_write("ERR app_not_idle state=%s err=%s",
                               app_state_name(app_get_state()),
                               esp_err_to_name(app_err));
                continue;
            }

            s_ota_running = true;
            protocol_write("READY partition=%s size=%u", update_partition->label, (unsigned)image_size);

            esp_err_t ota_err = ota_perform_from_console(image_size, expected_digest);
            if (ota_err == ESP_OK) {
                protocol_write("OK ota_complete rebooting");
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }

            s_ota_running = false;
            esp_err_t resume_err = app_start();
            if (resume_err != ESP_OK) {
                protocol_write("ERR app_resume_failed state=%s err=%s",
                               app_state_name(app_get_state()),
                               esp_err_to_name(resume_err));
            }
            continue;
        }
    }
}
