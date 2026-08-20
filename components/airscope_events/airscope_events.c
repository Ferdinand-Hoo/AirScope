#include "airscope_events.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static airscope_runtime_event_t s_events[AIRSCOPE_EVENT_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_boot_id;
static uint32_t s_next_sequence;
static SemaphoreHandle_t s_mutex;

esp_err_t airscope_events_init(uint32_t boot_id)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_events, 0, sizeof(s_events));
    s_head = 0;
    s_count = 0;
    s_boot_id = boot_id;
    s_next_sequence = 1;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void airscope_events_record(airscope_event_severity_t severity, const char *type,
                            const char *details_json)
{
    if (s_mutex == NULL || type == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    airscope_runtime_event_t *event = &s_events[s_head];
    memset(event, 0, sizeof(*event));
    event->boot_id = s_boot_id;
    event->sequence = s_next_sequence++;
    event->uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
    event->severity = severity;
    snprintf(event->type, sizeof(event->type), "%s", type);
    snprintf(event->details, sizeof(event->details), "%s",
             details_json != NULL ? details_json : "{}");
    s_head = (s_head + 1) % AIRSCOPE_EVENT_CAPACITY;
    if (s_count < AIRSCOPE_EVENT_CAPACITY) {
        ++s_count;
    }
    xSemaphoreGive(s_mutex);
}

size_t airscope_events_snapshot(airscope_runtime_event_t *out, size_t capacity)
{
    if (out == NULL || capacity == 0 || s_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t count = s_count < capacity ? s_count : capacity;
    size_t first = (s_head + AIRSCOPE_EVENT_CAPACITY - s_count) % AIRSCOPE_EVENT_CAPACITY;
    size_t skip = s_count - count;
    for (size_t i = 0; i < count; ++i) {
        out[i] = s_events[(first + skip + i) % AIRSCOPE_EVENT_CAPACITY];
    }
    xSemaphoreGive(s_mutex);
    return count;
}

uint32_t airscope_events_boot_id(void)
{
    return s_boot_id;
}

const char *airscope_event_severity_name(airscope_event_severity_t severity)
{
    static const char *names[] = {"info", "warning", "error"};
    return (unsigned)severity < sizeof(names) / sizeof(names[0]) ? names[severity] : "unknown";
}
