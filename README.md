# S0 Stromzähler ESP32 firmware

Firmware and Home Assistant custom integration for an ESP32-WROOM-32
development board reading three S0 pulse inputs through a PC817 optocoupler
board. The firmware does not control or alter any meter-side wiring.

## Features

- PlatformIO with the Arduino framework
- Wi-Fi station mode with non-blocking retry and automatic reconnect
- HTTP status page at `http://<ip-address>/` and JSON at `/api/status`
- mDNS hostname `s0-stromzaehler-esp32.local` when the local network supports mDNS
- ArduinoOTA updates, optionally protected by a local OTA password
- Serial logging at 115200 baud for boot, Wi-Fi, HTTP, mDNS, and OTA events
- Interrupt-based diagnostics for the three S0 inputs
- HTTP and serial display of raw input state, accepted edges, pulse counts,
  estimated current power, and accumulated diagnostic energy
- Persistent meter readings stored as small integer values in ESP32 NVS

## Hardware status

Configured diagnostic wiring:

- PC817 U1 to ESP32 GPIO25: Wärmepumpe S0 input
- PC817 U2 to ESP32 GPIO33: Ferienwohnung S0 input
- PC817 U3 to ESP32 GPIO27: Hauptwohnung S0 input
- All three GPIOs use `INPUT_PULLUP`; PC817 outputs are active-low.
- Meter mapping: GPIO25 is Wärmepumpe, GPIO33 is Ferienwohnung, and GPIO27
  is Hauptwohnung.
- S0 resolution is 1000 impulses/kWh, therefore one accepted pulse is 1 Wh or
  0.001 kWh. Current power is estimated from the gap between the two newest
  accepted pulses (`1 pulse/minute = 60 W`).

The active-low polarity is intentionally a named firmware configuration
(`OptocouplerInputs::kActiveLow`) rather than an implicit assumption. Verify
that idle state is `inactive (high)` and a meter pulse becomes `active (low)`.
The 10 ms debounce filters optocoupler/wiring transitions; it is not a
measurement or energy-accounting feature.

Still to verify before adding any integration:

- USB-to-serial chip, serial device path, flash size, and exact ESP32 module
- Stable 5 V/3.3 V power arrangement for the selected development board
- Each meter produces one accepted falling edge and pulse count increment per
  physical S0 pulse.

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
pio run -e esp32ota -t upload --upload-port s0-stromzaehler-esp32.local
```

PlatformIO will use the ArduinoOTA protocol. The local OTA password is read
from `include/secrets.h`; it is never stored in Git. If mDNS is unavailable,
use the device IP address shown in the serial log or status page.

## Verification endpoints

- `GET /` — human-readable status page
- `GET /api/status` — machine-readable JSON containing network state plus an
  `s0_inputs` object for all channels (name, raw level, active state, edges,
  raw pulses, Wh, kWh, estimated current W, and last pulse timing)
- `POST /api/meters/<1|2|3>/reading` — sets a meter reading in whole Wh. This
  endpoint requires HTTP Basic authentication with username `admin` and the
  existing local OTA password. The request body is form data:
  `energy_wh=<whole-number>`.

Meter readings are persisted as small key-value entries in ESP32 NVS rather
than in a database. NVS is appropriate for these three values and has built-in
wear levelling. The firmware saves automatically after 1 kWh of change or at
least every six hours when a value has changed; a manually set value is saved
immediately. Raw `pulses` remain a diagnostic count since boot, while
`energy_wh` and `energy_kwh` are the persisted meter readings.

The firmware keeps its main loop running while Wi-Fi is unavailable. It does
not block waiting for a connection and retries in the background with a
bounded backoff.

## Home Assistant

The repository contains the custom integration in
`custom_components/s0_stromzaehler`. Install the repository as a custom
**Integration** repository in HACS, download it, and restart Home Assistant.
Then add **S0 Stromzähler ESP32** in *Settings → Devices & services* and enter
the ESP32 hostname (or its local IP address) and HTTP port. This connection
data is stored only in Home Assistant's config entry, never in Git.

The integration polls `/api/status` every 10 seconds and creates six sensors:

- Wärmepumpe, Ferienwohnung, Hauptwohnung: current power in W
- Wärmepumpe, Ferienwohnung, Hauptwohnung: accumulated energy in kWh

Power sensors use the `measurement` state class. Energy sensors use the
`energy` device class and `total` state class, making them suitable for the
Home Assistant Energy Dashboard and long-term statistics. Before adding the
energy sensors, set each real mechanical meter reading with the authenticated
ESP API so the first imported value is correct.

## Development notes

The initial PlatformIO target is `esp32dev`, the generic target appropriate
for an ESP32-WROOM-32 development board. Pulse counters reset on firmware
restart and remain diagnostic only; the meter readings are persistent.
