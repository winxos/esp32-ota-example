#include "log_utils.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void log_vwrite(const char *level, const char *fmt, va_list args)
{
    TickType_t ticks = xTaskGetTickCount();
    uint64_t uptime_ms = ((uint64_t)ticks * 1000ULL) / configTICK_RATE_HZ;
    uint32_t total_seconds = uptime_ms / 1000;
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;
    uint32_t millis = uptime_ms % 1000;

    printf("[%02lu:%02lu:%02lu.%03lu] [%s] ",
           (unsigned long)hours,
           (unsigned long)minutes,
           (unsigned long)seconds,
           (unsigned long)millis,
           level);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
}

void log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vwrite("DEBUG", fmt, args);
    va_end(args);
}

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vwrite("INFO", fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vwrite("WARN", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vwrite("ERROR", fmt, args);
    va_end(args);
}
