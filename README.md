# S0 Stromzähler ESP32 firmware

Base firmware for an ESP32-WROOM-32 development board that will eventually read
S0 pulses from an electricity meter through an optocoupler board. The S0 input
and optocoupler wiring are deliberately not implemented in this milestone. The
firmware only exercises the ESP32, Wi-Fi, HTTP status page, and OTA update path.

## Features

- PlatformIO with the Arduino framework
- Wi-Fi station mode with non-blocking retry and automatic reconnect
- HTTP status page at `http://<ip-address>/` and JSON at `/api/status`
- mDNS hostname `s0-stromzaehler-esp32.local` when the local network supports mDNS
- ArduinoOTA updates, optionally protected by a local OTA password
- Serial logging at 115200 baud for boot, Wi-Fi, HTTP, mDNS, and OTA events
- An explicit, inactive `OptocouplerInputs` boundary for the next milestone

## Hardware status

Verified so far:

- ESP32-WROOM-32 development board: reported as available, but USB identity
  and exact board variant still require a connected-device check.
- Four-channel optocoupler board, DIN-rail supply: reported as available;
  no pins are configured and no electrical connection is assumed.
- No electricity-meter or mains-side wiring is part of this firmware.

Still to verify:

- USB-to-serial chip, serial device path, flash size, and exact ESP32 module
- Stable 5 V/3.3 V power arrangement for the selected development board
- S0 pulse interface polarity, pulse voltage/current, isolation, and GPIO
  mapping before any input code is added

## Local setup

Install PlatformIO Core, then from this directory run:

```text
pio run
pio device list
```

Keep credentials in the ignored local file `include/secrets.h`:

```text
cp include/secrets.example.h include/secrets.h
cp platformio.local.ini.example platformio.local.ini
```

Edit `include/secrets.h` with the Wi-Fi SSID, Wi-Fi password, and a strong OTA
password. Put the same OTA password in `platformio.local.ini` after
`--auth=`. Both files are intentionally ignored and must never be committed.
If `secrets.h` is absent, the firmware still builds but reports that Wi-Fi is
disabled.

## First USB flash

Connect the ESP32 by USB, confirm the device path with `pio device list`, and
flash the firmware:

```text
pio run -t upload --upload-port /dev/cu.<device>
pio device monitor --port /dev/cu.<device> --baud 115200
```

If the board does not enter the bootloader automatically, hold its BOOT button
while starting the upload and release it when writing begins. The serial log
should show the firmware version, Wi-Fi connection, IP address, HTTP server,
mDNS, and OTA readiness.

## Subsequent OTA updates

After the first successful USB flash and Wi-Fi connection, update over the
network using either the mDNS hostname or the resolved IP address:

```text
pio run -t upload --upload-port s0-stromzaehler-esp32.local
```

PlatformIO will use the ArduinoOTA protocol. The local OTA password is read
from `include/secrets.h`; it is never stored in Git. If mDNS is unavailable,
use the device IP address shown in the serial log or status page.

## Verification endpoints

- `GET /` — human-readable status page
- `GET /api/status` — machine-readable JSON containing hostname, firmware
  version, uptime, Wi-Fi state, IP address, and RSSI

The firmware keeps its main loop running while Wi-Fi is unavailable. It does
not block waiting for a connection and retries in the background with a
bounded backoff.

## Development notes

The initial PlatformIO target is `esp32dev`, the generic target appropriate
for an ESP32-WROOM-32 development board. Once the connected board is inspected,
the board setting should be changed only if the verified USB/flash hardware
requires it. No S0 pulse counting or electricity-meter operation is present.
