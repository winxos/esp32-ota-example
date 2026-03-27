#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "log_utils.h"
#include "ota_service.h"

static void heartbeat_task(void *arg)
{
    (void)arg;

    while (true) {
        if (!ota_running()) {
            const esp_partition_t *configured = esp_ota_get_boot_partition();
            log_info("HEARTBEAT boot=%s app=%s free_heap=%u",
                     configured ? configured->label : "none",
                     app_state_name(app_get_state()),
                     (unsigned)esp_get_free_heap_size());
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ota_console_init();
    ota_mark_running_partition_valid();
    xTaskCreate(app_task, "app_task", 6144, NULL, 4, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 3072, NULL, 1, NULL);
    xTaskCreate(ota_console_task, "ota_console_task", 6144, NULL, 5, NULL);
}
