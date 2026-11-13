# PhxTraffic

ESP32-WROOM-32E Phoenix-area traffic display using an Inland 2.13-inch monochrome e-ink module and the AZ 511 Get Events API.

## Hardware configuration

The repository photo identifies the Inland 2.13-inch product, but does not show its controller or pin labels. The existing repository sketches use `GxEPD2_213_B74`, SPI pins 18/19/23, display pins CS/DC/RST/BUSY 5/17/16/4, and rotation 1. Those are provisional defaults only; verify the exact panel controller, wiring, and orientation against the display's PCB marking or product documentation before powering/building. Adjust `config.h` if the hardware differs.

The current driver profile is selected explicitly in `config.h`. The code fails to compile if that selection is removed, so a different panel must be deliberately configured rather than silently assumed.

## Setup

1. Install PlatformIO and build from this directory with `pio run`.
2. Copy `secrets.h.example` to `secrets.h`; this file is ignored by Git.
3. Set `WIFI_SSID`, `WIFI_PASSWORD`, and the approved `AZ511_API_KEY` in `secrets.h`. Do not put an issued key in source control. If the key is empty or remains `REPLACE_WITH_APPROVED_AZ511_KEY`, the firmware displays the bundled Tucson roadwork sample event as `AZ 511 TEST DATA`. In this test mode it does not connect to Wi-Fi, synchronize time, or make a live request. The Tucson event is intentionally shown even though it falls outside the Phoenix metro bounds.
4. Flash with `pio run --target upload`; use `pio device monitor` to see connection and API errors.

With an approved non-empty API key, the firmware calls only `https://az511.com/api/v2/get/event` for traffic data, requests JSON, and sets the API `StartDate` to the current Arizona calendar date. It filters returned events locally to the configurable Phoenix metro bounding box, using event coordinates for metro selection and event start/end times when available. Up to three events are rendered, with closures first and accidents/incidents next. Empty and placeholder keys never issue a live request.

Event text is measured against the active display width and wrapped by words; oversized individual words are split at the widest fitting character boundary. The e-ink screen shows one event at a time, paginating its complete roadway/title and description every 15 seconds, then advances through the other selected events. This keeps all text readable instead of clipping it; up to three events are still selected and cycled. Live refresh remains every five minutes, and each live API request is preceded by the three-frame car animation. If a request fails or returns invalid data, the last good event data remains available.

In test mode, the car animation runs once at startup before the sample event is displayed. The local sample then paginates without repeating the animation or making periodic network requests.

## Notes

- The API key is sent as the endpoint's `key` query parameter and is never logged.
- HTTPS server certificates are validated with the ESP32 Arduino core's built-in full Mozilla-root CA bundle. The firmware requires `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` and `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL` at compile time. It does not disable verification or fall back to an unverified connection.
- Before making the first API request after Wi-Fi connects, the ESP32 synchronizes its clock over SNTP (`pool.ntp.org`, then `time.cloudflare.com`). Requests remain skipped until the clock is valid, because certificate validity dates cannot be checked reliably with an unset clock. SNTP carries no AZ 511 key; the HTTPS connection still requires a trusted certificate chain and matching hostname.
- The CA bundle is embedded by the ESP32 platform/framework version used at build time. Keep PlatformIO's Espressif32 platform/framework current, rebuild and deploy firmware updates to refresh its Mozilla trust roots, and monitor TLS failures for upstream certificate changes or expired roots. Never work around certificate errors by disabling verification.
- If an event has no coordinates, it is excluded because its metro location cannot be established.
