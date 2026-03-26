#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "app_state.h"
#include "ota_service.h"
#include "usb_task.h"

#define BLINK_GPIO CONFIG_BLINK_GPIO

static app_state_t s_app_state;
static uint8_t s_led_state;

static void blink_led(void)
{
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

static void led_task(void *arg)
{
    (void)arg;
    configure_led();

    while (true) {
        blink_led();
        s_led_state = !s_led_state;
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BLINK_PERIOD));
    }
}

static void heartbeat_task(void *arg)
{
    app_state_t *state = (app_state_t *)arg;

    while (true) {
        if (!state->ota_in_progress) {
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *configured = esp_ota_get_boot_partition();
            printf("HEARTBEAT running=%s boot=%s free_heap=%" PRIu32 "\n",
                   running ? running->label : "none",
                   configured ? configured->label : "none",
                   esp_get_free_heap_size());
            fflush(stdout);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    usb_console_init();
    ota_mark_running_partition_valid();

    xTaskCreate(led_task, "blink_task", 2048, NULL, 1, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 3072, &s_app_state, 1, NULL);
    xTaskCreate(usb_task, "usb_task", 6144, &s_app_state, 5, NULL);
}
