#include "ota_service.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/usb_serial_jtag.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"

#include "driver/usb_serial_jtag.h"

static esp_err_t ota_fail(const char *stage, esp_err_t err)
{
    printf("ERR stage=%s err=%s\n", stage, esp_err_to_name(err));
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
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
            puts("OK app_confirmed");
        } else {
            printf("ERR confirm_failed %s\n", esp_err_to_name(err));
        }
        fflush(stdout);
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
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

esp_err_t ota_perform_from_console(app_state_t *state,
                                   size_t image_size,
                                   const uint8_t expected_digest[OTA_SHA256_LEN])
{
    (void)state;
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

    printf("OTA stage=write_done size=%u\n", (unsigned)image_size);
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        free(buffer);
        return ota_fail("ota_end", err);
    }

    puts("OTA stage=ota_end_ok");
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));

    puts("OTA stage=image_validated");
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        free(buffer);
        return ota_fail("set_boot", err);
    }

    printf("OTA stage=set_boot_ok partition=%s\n", update_partition->label);
    fflush(stdout);
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));

    free(buffer);
    return ESP_OK;
}
