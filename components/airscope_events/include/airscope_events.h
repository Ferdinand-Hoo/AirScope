#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIRSCOPE_EVENT_CAPACITY 128
#define AIRSCOPE_EVENT_TYPE_MAX_LEN 31
#define AIRSCOPE_EVENT_DETAILS_MAX_LEN 159

typedef enum {
    AIRSCOPE_EVENT_INFO = 0,
    AIRSCOPE_EVENT_WARNING,
    AIRSCOPE_EVENT_ERROR,
} airscope_event_severity_t;

typedef struct {
    uint32_t boot_id;
    uint32_t sequence;
    uint64_t uptime_ms;
    airscope_event_severity_t severity;
    char type[AIRSCOPE_EVENT_TYPE_MAX_LEN + 1];
    char details[AIRSCOPE_EVENT_DETAILS_MAX_LEN + 1];
} airscope_runtime_event_t;

esp_err_t airscope_events_init(uint32_t boot_id);
void airscope_events_record(airscope_event_severity_t severity, const char *type,
                            const char *details_json);
size_t airscope_events_snapshot(airscope_runtime_event_t *out, size_t capacity);
uint32_t airscope_events_boot_id(void);
const char *airscope_event_severity_name(airscope_event_severity_t severity);

#ifdef __cplusplus
}
#endif
