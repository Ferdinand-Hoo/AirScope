# Own WiFi configuration persistence

The firmware will configure the ESP-IDF WiFi driver for RAM-backed settings and
persist the versioned AP Configuration only through the project configuration
store. This avoids conflicting application and driver NVS copies and allows a
Configuration Transaction or Channel Switch Operation to become persistent
only after application, readback, and rollback rules have completed.
