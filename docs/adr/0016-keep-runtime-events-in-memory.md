# Keep runtime events in memory

The platform will retain the latest 128 Runtime Events in a RAM ring and expose
them as JSON, while detailed diagnostics remain available over USB serial.
Runtime Events reset on reboot and use a boot identifier, sequence number, and
monotonic uptime rather than wall-clock time; high-frequency event logging will
not write flash, avoiding wear and a false promise of a persistent audit trail.
