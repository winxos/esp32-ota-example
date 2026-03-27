#include "app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_utils.h"
#include "ota_service.h"

#define APP_LOOP_DELAY_MS 10

typedef struct {
    volatile app_state_t state;
    bool initialized;
    bool started;
} app_context_t;

static app_context_t s_app = {
    .state = APP_STATE_STOPPED,
};

static void app_set_state(app_state_t state)
{
    s_app.state = state;
}

app_state_t app_get_state(void)
{
    return s_app.state;
}

const char *app_state_name(app_state_t state)
{
    switch (state) {
    case APP_STATE_STOPPED:
        return "stopped";
    case APP_STATE_STARTING:
        return "starting";
    case APP_STATE_IDLE:
        return "idle";
    case APP_STATE_BUSY:
        return "busy";
    case APP_STATE_STOPPING:
        return "stopping";
    default:
        return "unknown";
    }
}

static void app_init_impl(app_context_t *app)
{
}

static void app_start_impl(app_context_t *app)
{
    (void)app;
}

static void app_stop_impl(app_context_t *app)
{
    (void)app;
}

static void app_loop_impl(app_context_t *app)
{
    (void)app;
    vTaskDelay(pdMS_TO_TICKS(APP_LOOP_DELAY_MS));
}

static bool app_on_received_impl(app_context_t *app, const uint8_t *data, size_t length)
{
    (void)app;
    (void)data;
    if (ota_running()) 
    {
        return false;
    }
    log_debug("rx %u bytes", (unsigned)length);
    return false;
}

bool app_is_idle(void)
{
    return app_get_state() == APP_STATE_IDLE;
}

bool app_on_received(const uint8_t *data, size_t length)
{
    if (!s_app.started || data == NULL || length == 0) {
        return false;
    }

    return app_on_received_impl(&s_app, data, length);
}

esp_err_t app_start(void)
{
    if (s_app.started) {
        app_set_state(APP_STATE_IDLE);
        return ESP_OK;
    }

    app_set_state(APP_STATE_STARTING);
    app_start_impl(&s_app);
    s_app.started = true;
    app_set_state(APP_STATE_IDLE);
    return ESP_OK;
}

esp_err_t app_stop(void)
{
    if (!app_is_idle()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_app.started) {
        app_set_state(APP_STATE_STOPPED);
        return ESP_OK;
    }

    app_set_state(APP_STATE_STOPPING);
    app_stop_impl(&s_app);
    s_app.started = false;
    app_set_state(APP_STATE_STOPPED);
    return ESP_OK;
}

void app_task(void *arg)
{
    (void)arg;

    if (!s_app.initialized) {
        app_init_impl(&s_app);
        s_app.initialized = true;
    }

    ESP_ERROR_CHECK(app_start());

    while (true) {
        if (s_app.started) {
            app_loop_impl(&s_app);
        } else {
            vTaskDelay(pdMS_TO_TICKS(APP_LOOP_DELAY_MS));
        }
    }
}
