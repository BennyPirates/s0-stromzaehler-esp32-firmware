# S0 wiring and diagnostic handover

This document records the physical wiring and the current diagnostic state.
It intentionally contains no credentials, IP addresses, OTA details, or meter
readings.

## Current firmware scope

- The ESP32 runs the S0 diagnostic firmware (`0.3.5`).
- GPIOs use `INPUT_PULLUP` and treat a low level as active.
- The HTTP status endpoint reports the raw state, accepted rising/falling edges,
  and pulse counter of all three channels.
- Capture is interrupt-based with a 10 ms debounce period.
- The repository includes a Home Assistant custom integration that exposes the
  three live-power and three energy readings.
- Meter readings can be read from the status endpoint and set through an
  authenticated local API. They are saved in ESP32 NVS, not in a database.

## ESP32 / PC817 output side

View the HY-M154 with `817 Module` at the top and `HY-M154` at the bottom.
On the **right** side, the upper terminal of each pair is `U` and the lower
terminal is `G`.

| Channel | HY-M154 output `U` wire | ESP32 GPIO | HY-M154 output `G` wire |
| --- | --- | --- | --- |
| 1 — Wärmepumpe | green | GPIO25 | black |
| 2 — Ferienwohnung | yellow | GPIO33 | white |
| 3 — Hauptwohnung | orange | GPIO27 | grey |

- `U1`, `U2`, and `U3` go to GPIO25, GPIO33, and GPIO27 respectively.
- The three right-side `G` terminals form the common **ESP/output-side ground**
  and go only to ESP32 GND.
- The four red channel jumpers are removed. Do not reinstall them.

## S0 / PC817 input side

On the **left** side, the upper terminal of each pair is `IN` and the lower
terminal is `G`.

The visible wire colours are:

| Channel | HY-M154 input `IN` (upper) | HY-M154 input `G` (lower) |
| --- | --- | --- |
| 1 | red | violet |
| 2 | red | blue |
| 3 | red | brown / dark red |

The intended current-loop arrangement is:

```text
S0 input supply +5 V
  ├─ red -> IN1
  ├─ red -> IN2
  └─ red -> IN3

meter 1 S0+ -> violet -> G1 (Wärmepumpe)
meter 2 S0+ -> blue    -> G2
meter 3 S0+ -> brown   -> G3 (Hauptwohnung)

meter 1/2/3 S0− -> common S0 input-supply 0 V
```

`S0−` is the common return of the **input-side** current loops. It is not an
ESP GPIO or an ESP ground connection.

## Power supplies and isolation

The installed supply is a Mean Well HDR-15-5 (5 V, 2.4 A). It currently powers
both the ESP32 and the S0 input loops, which joins their 0 V references and
defeats the optocouplers' isolation.

A second, identical HDR-15-5 has been ordered. It is now installed and the
input-side and ESP-side 0 V references are separate.

```text
Existing HDR-15-5
  +5 V / 0 V -> ESP32 power and right-side PC817 output ground only

New HDR-15-5
  +5 V       -> the three red input wires (IN1/IN2/IN3)
  0 V        -> common S0− return of the three meters only
```

The two DC 0 V outputs must not be connected anywhere. A common AC neutral on
the mains input side does not connect the two isolated DC outputs.

## Confirmed hardware result

- PC817 channels 1–3 blink individually and synchronously with their matching
  meter's S0 pulse indication.
- The three S0 inputs are now electrically independent.
- Meter mapping: Wärmepumpe → IN1/U1/GPIO25, Ferienwohnung →
  IN2/U2/GPIO33, Hauptwohnung → IN3/U3/GPIO27.

## OTA verification status

GPIO26 was found to be held at 0 V even with the U2 signal wire disconnected.
It is no longer used. Ferienwohnung now uses GPIO33; its U2 signal wire must
be moved from D26 to D33 while G2 remains connected to ESP GND.

GPIO33 is therefore **not yet verified**. Before committing, check its
channel independently:

- PC817 channel 2 LED off at idle but GPIO33 low: inspect the right-side U2
  (yellow) and G2 (white) wiring.
- PC817 channel 2 LED permanently on: inspect only its left-side S0 input loop.
- A healthy idle state reports GPIO33 as `high` and inactive; a pulse briefly
  changes it to low and increments only Ferienwohnung's counter.

## Next-session checklist

1. Confirm the four red HY-M154 jumpers are still removed.
2. Check the status endpoint at idle: every channel should be `high` and
   inactive.
3. Produce a known load on one meter at a time. Only that channel's PC817 LED,
   edge counters, and pulse counter should react.
4. Resolve and verify GPIO33 / Ferienwohnung independently before considering
   a Home Assistant integration.
5. Only after successful hardware verification: review the Git diff, commit,
   and push. Do not commit local configuration, credentials, device addresses,
   or meter data.
