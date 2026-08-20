# AirScope

AirScope turns a Waveshare ESP32-S3-Touch-LCD-1.69 into a standalone,
configurable 2.4 GHz Wi-Fi experimental access point. It combines an ESP-IDF
firmware, an on-device LVGL status interface, and an embedded Preact management
application.

The project is intended for repeatable wireless configuration and compatibility
experiments without relying on a commercial router.

## Features

- Standalone SoftAP operation on China 2.4 GHz channels 1 through 13
- Live channel switching with Channel Switch Announcement (CSA)
- Persistent, validated AP and radio configuration
- Open, WPA2, WPA3, transition, and expert security profiles
- Local HTTPS management API and browser interface
- Browser sessions and revocable automation tokens
- Touch-enabled device status and provisioning views
- Volatile runtime event history and USB serial diagnostics
- GPIO40 five-second physical recovery flow

The first release does not provide upstream Wi-Fi, routing, NAT, captive portal,
OTA, Enterprise authentication, WPS, packet capture, or CSI.

## Hardware And Toolchain

- Waveshare ESP32-S3-Touch-LCD-1.69
- ESP-IDF 6.0.2
- Node.js 22 or later
- npm

ESP-IDF dependencies are declared in component manifests and pinned by
`dependencies.lock`.

## Build

Set up the ESP-IDF environment first:

```bash
source /path/to/esp-idf/export.sh
```

Install the browser application dependencies and generate the compressed assets
embedded by the firmware:

```bash
cd web
npm ci
npm run build
cd ..
```

Build the firmware:

```bash
idf.py set-target esp32s3
idf.py build
```

Flash the board and open the serial monitor:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with the serial port used by the board. Exit the monitor
with `Ctrl+]`.

## First Use

1. Power on the board and wait for the provisioning view.
2. Connect to the displayed `AirScope-<MAC-last-6>` Wi-Fi network.
3. Open `https://192.168.4.1`.
4. Sign in with the management credential shown on the device.

The default AP is open and starts on channel 1. The management network uses
`192.168.4.1/24`.

To restore the default AP configuration and rotate management credentials, hold
GPIO40 for five seconds during startup.

## Web Development

Run the management interface locally:

```bash
cd web
npm ci
npm run dev
```

Run the browser tests:

```bash
cd web
npm run e2e
```

The browser build writes compressed assets to `components/airscope_api/web/`.
Those generated files are committed because the firmware embeds them directly.

## Repository Layout

```text
components/             ESP-IDF components
  airscope_api/         HTTPS API and embedded web assets
  airscope_auth/        Credentials, sessions, and automation tokens
  airscope_board/       Waveshare board adapter
  airscope_config/      Configuration model and persistence
  airscope_display/     LVGL status interface
  airscope_events/      Runtime event ring
  airscope_wifi/        SoftAP and channel control
main/                   Firmware entry point
web/                    Preact management application
docs/                   Design document and architecture decisions
```

See [docs/design-v1.md](docs/design-v1.md) for the system design and
[CONTEXT.md](CONTEXT.md) for the project domain language.

## Security Notes

- AirScope is a laboratory tool. Legacy security modes should only be used in
  isolated test environments.
- The device uses a locally generated HTTPS identity, so a browser may require
  explicit certificate acceptance.
- Do not commit exported credentials, private keys, automation tokens, or local
  environment files.

## License

No license has been selected yet. All rights are reserved by the repository
owner unless a license file is added.
