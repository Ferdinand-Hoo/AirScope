# Persist AP configuration with physical recovery

AP Configuration changes will take effect immediately and become the
Persistent Configuration used after restart. If that configuration makes the
Management Interface unreachable, the operator can hold the board's
PWR/GPIO40 button for five seconds to restore a known Default Configuration;
automatic timeout rollback and restart-to-default behavior are intentionally
excluded. Recovery also invalidates the previous Management Credential and
restores it to `admin`. The Default Configuration uses an open
`AirScope-<MAC-last-6>` SSID with no WiFi password, channel 1, visible SSID,
four-client limit, 100 TU beacon interval, and CSA count 3. The Status Display
shows the open-network state, management password, and WiFi QR code during
initial provisioning.
