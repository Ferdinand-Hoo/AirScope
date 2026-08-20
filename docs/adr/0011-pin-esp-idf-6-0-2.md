# Pin ESP-IDF 6.0.2

Firmware development will use native ESP-IDF `v6.0.2`, pinned rather than
tracking the development branch, and will not use the Arduino framework. This
keeps the project on the current stable ESP32-S3 APIs needed for WiFi CSA,
HTTPS serving, NVS persistence, and FreeRTOS integration while preserving
direct control over their behavior.
