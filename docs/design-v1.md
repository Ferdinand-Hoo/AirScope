# AirScope V1 Design

AirScope V1 turns the Waveshare ESP32-S3-Touch-LCD-1.69 into a standalone,
configurable 2.4 GHz Experimental Access Point. It is managed locally through
an embedded HTTPS application and a versioned JSON API.

## Scope

V1 provides:

- standalone SoftAP operation on China channels 1 through 13
- live channel changes through CSA without stopping WiFi
- persistent, atomic AP configuration with physical recovery
- common Security Profiles and validated Expert Security Settings
- validated Radio Profiles
- HTTPS Web management and script automation
- touch-enabled status and provisioning views
- volatile runtime events and USB diagnostics

V1 does not provide upstream WiFi, routing, NAT, DNS forwarding, captive
portal, OTA, Enterprise authentication, WPS, DPP, LR, FTM, CSI, sniffing, or
Vendor IE experiments.

## Technology Baseline

- Waveshare ESP32-S3-Touch-LCD-1.69
- ESP-IDF 6.0.2
- Waveshare managed board BSP, pinned through the component lock
- LVGL through Espressif's LVGL port
- native ESP-IDF WiFi, esp-netif, NVS, HTTPS server, mbedTLS, and FreeRTOS
- Preact, TypeScript, and Vite for the embedded browser application

## Firmware Components

**boot_coordinator**
Initializes storage, identity, board services, configuration, WiFi, management,
display, and diagnostics in dependency order.

**board_adapter**
Wraps the Waveshare BSP for LCD, touch, backlight, GPIO40 recovery input, and
board identity.

**config_model**
Owns the versioned AP Configuration schema, defaults, presets, validation,
redaction, and comparison.

**config_store**
Persists one authoritative versioned configuration and authentication metadata
in project NVS namespaces. ESP-IDF WiFi settings use RAM storage.

**ap_controller**
Translates validated Security and Radio Profiles into ESP-IDF settings,
executes atomic configuration and CSA operations, and performs runtime
readback.

**auth_service**
Owns password verification, browser sessions, Automation Tokens, rate limits,
and credential rotation.

**management_server**
Serves the compressed browser application and `/api/v1` over HTTPS.

**runtime_events**
Stores the latest 128 Runtime Events in a synchronized RAM ring.

**status_ui**
Renders provisioning, AP status, clients, Operation Results, Runtime Events,
and display controls through LVGL.

## Boot Flow

1. Initialize NVS and the board adapter.
2. Start the display with an initializing state.
3. Detect an in-progress GPIO40 five-second recovery request.
4. Load or create the device HTTPS identity.
5. Load and validate the Persistent Configuration.
6. If absent or invalid, generate the Default Configuration and credentials.
7. Set the China regulatory domain and `WIFI_MODE_AP`.
8. Apply the configuration before starting WiFi.
9. Start the fixed management network and DHCP server.
10. Start HTTPS management and the status UI.
11. Show provisioning secrets only when newly generated.

Physical recovery preserves the device HTTPS identity, restores the Default
Configuration, rotates both AP and management passwords, revokes sessions and
Automation Tokens, and restarts into a provisioning view.

## Default Configuration

- SSID: `AirScope-<MAC-last-6>`
- authentication: open network
- pairwise cipher: CCMP
- PMF: optional
- channel: 6
- bandwidth: HT20
- protocol: b/g/n
- SSID visibility: visible
- maximum clients: 4
- beacon interval: 100 TU
- DTIM period: 1
- CSA count: 3
- independent random AP and management passwords

The management network is fixed at `192.168.4.1/24`, with DHCP leases from
`192.168.4.100` through `192.168.4.199`.

## Configuration Model

The persisted schema contains:

- schema version
- SSID bytes and visibility
- AP credential
- authentication mode
- pairwise cipher
- PMF requirement
- SAE-PWE method
- primary channel and HT40 secondary-channel direction
- CSA count
- protocol mode
- bandwidth
- maximum transmit power
- maximum clients
- beacon interval
- DTIM period

Passwords and tokens are never returned by configuration read APIs. The
management password is stored as a salted password hash. Automation Tokens are
random high-entropy values stored only as hashes after their one-time display.

## Validation

Firmware validation is authoritative. The browser mirrors these rules for fast
feedback but cannot bypass them.

Validation includes:

- SSID and password length and encoding
- security-mode and cipher compatibility
- mandatory PMF behavior for WPA3
- SAE settings only when applicable
- China channel range
- HT40 compatibility with 802.11n and valid secondary-channel placement
- hardware-supported transmit-power steps
- client, beacon, DTIM, and CSA ranges
- rejection of all excluded capabilities

The capabilities endpoint exposes exact supported values so automation does
not hard-code driver enums or assumptions.

## Configuration Transaction

1. Parse and schema-check the complete proposed non-channel configuration.
2. Validate all cross-field constraints.
3. Capture the current Applied Configuration.
4. Apply the proposed settings to the running SoftAP.
5. Read back every supported runtime field.
6. Persist only when application and readback succeed.
7. On failure, restore and verify the previous Applied Configuration.
8. Record one Operation Result and Runtime Event.

If persistence fails after runtime application, the previous runtime
configuration is restored so Applied and Persistent Configuration do not
silently diverge. A failed rollback enters a visible degraded state and
requires physical recovery.

## Channel Switch Operation

Channel changes cannot be combined with other configuration changes.

1. Validate primary channel, bandwidth, secondary direction, and CSA count.
2. Capture the current channel state.
3. Configure the CSA count used by the SoftAP.
4. Call `esp_wifi_set_channel()` while WiFi remains started.
5. Poll `esp_wifi_get_channel()` until the requested channel is reported.
6. Use a timeout derived from CSA count and beacon interval with a fixed margin.
7. Persist the new channel only after successful readback.
8. On failure, attempt a CSA switch back to the previous channel.
9. Record the result and whether rollback succeeded.

The guarantee is that the AP remains started and initiates CSA. Browser request
continuity and station CSA compatibility are not guaranteed.

## Management API

Initial endpoint groups:

- `POST /api/v1/session`
- `DELETE /api/v1/session`
- `GET /api/v1/capabilities`
- `GET /api/v1/status`
- `GET /api/v1/config`
- `PUT /api/v1/config`
- `POST /api/v1/channel-switch`
- `GET /api/v1/clients`
- `GET /api/v1/events`
- `POST /api/v1/tokens`
- `DELETE /api/v1/tokens/{id}`
- `PUT /api/v1/credential`

Browser authentication uses a short-lived `Secure`, `HttpOnly`,
`SameSite=Strict` cookie and CSRF protection. Automation uses a Bearer
Automation Token. Mutation responses contain an operation identifier and a
structured Operation Result.

## Status UI

Touch interaction navigates but does not edit AP Configuration. Views include:

- provisioning QR code and temporary credentials
- AP identity, security, channel, bandwidth, power, and management address
- connected client count and details
- latest Operation Result
- recent Runtime Events
- brightness and display controls

Provisioning secrets disappear after the initialization window and can be
regenerated only through physical recovery.

## Runtime Events

Each event includes:

- boot identifier
- monotonically increasing sequence
- uptime
- event type
- severity
- redacted structured details

The RAM ring resets at boot. Web clients can retrieve and export it as JSON.
Detailed ESP-IDF logs remain available over USB serial.

## Build Layout

The repository will contain:

```text
components/
  airscope_board/
  airscope_config/
  airscope_wifi/
  airscope_auth/
  airscope_api/
  airscope_events/
  airscope_display/
main/
web/
docs/
```

The web build produces compressed immutable assets consumed by the firmware
build. No CDN or network access is required at runtime.

## Verification

Host-side tests cover:

- configuration serialization and schema migration
- every security and radio validation rule
- preset expansion
- redaction
- atomic operation state transitions
- authentication and token logic
- Runtime Event ring behavior

Browser tests cover:

- login and session expiry
- responsive configuration forms
- preset and expert validation
- channel-switch workflow
- token creation and one-time display
- recovery and degraded-state presentation

Hardware tests cover:

- first boot and provisioning
- persistence across restart
- GPIO40 five-second recovery
- each supported Security Profile
- representative expert combinations
- channels 1, 6, and 13
- CSA with an associated station and no `esp_wifi_stop()` call
- HT20 and valid HT40 combinations
- transmit-power readback
- client limit, beacon interval, and DTIM readback
- HTTPS authentication, session expiry, and Automation Tokens
- display and touch navigation
- failure injection for apply, persistence, and rollback

Independent packet capture is optional for V1 and is not part of the Applied
Configuration guarantee.
