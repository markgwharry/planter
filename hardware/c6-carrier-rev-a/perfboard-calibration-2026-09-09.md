# C6 perfboard moisture calibration - 2026-09-09

Applies to Mark's assembled 50 x 70 mm Planter perfboard with XIAO ESP32-C6,
AO3401A-switched sensor supply and B/W WeAct 1.54-inch display. This is one
physical probe's relative air/water calibration, not a general C6 constant or
a measurement of volumetric soil-water content.

## Measurements

Powered by USB; battery disconnected as requested for bench calibration.
The user positioned the sensing section, keeping the electronics dry. Firmware
uses the existing 100 ms rail-settle delay and averages 16 ADC samples per raw
reading. The debug loop repeats about every 3.3 seconds. Both captures include
one initial setup reading followed by nine debug-loop readings.

| Condition | Ten raw readings | Median | Range |
| --- | --- | --- | --- |
| Clean dry probe in air | 2661, 2661, 2661, 2661, 2661, 2660, 2660, 2660, 2660, 2660 | 2660.5 | 2660-2661 |
| Sensing section stationary in water | 1186, 1185, 1185, 1185, 1185, 1183, 1185, 1184, 1183, 1183 | 1185 | 1183-1186 |

Capture UTC times: dry 14:22:59; wet 14:24:50. Raw JSON records remain in the
local enclosure workspace under `work/c6-calibration/captures/`.

## Applied values

- `MOISTURE_DRY = 2661` (dry median rounded up).
- `MOISTURE_WET = 1185` (wet median).
- Restricted to `PLANTER_XIAO_C6` in `firmware-v2/planter_v2.cpp`.
- S3 values remain 3160 / 1300.
- Normal repository firmware retains `DEBUG_MODE=false` and 30-minute sleep.
- `BATT_DIV=2.0` and `BATT_TRIM=1.0` are unchanged.

Temporary debug firmware was built in the separate local `work/c6-calibration`
project. It should not remain installed for battery-powered field use.

## Normal firmware restored

The calibrated C6 normal build compiled and was uploaded with flash-hash
verification on 2026-09-09. Application image SHA-256:
`ce2217f6b25c20e9c11a209afe5ad35806b891f4ba5ff019497b4e95c22aee4d`.

The first boot reported raw 1181 -> 100% with the probe still in water, completed
the display-update routine, published successfully to MQTT, then printed
`Sleeping for 30 minutes...`. It did not enter the debug loop. This verifies
normal-mode execution through the sleep step, not measured sleep current.
The existing display-library GPIO initialisation warnings remain; no driver
or pin-map changes were made as part of this calibration.

## Remaining checks

Confirm near-zero dry and near-100 wet after the calibrated normal build is
flashed. Firmware completion and serial output do not replace visual checks.
Check behaviour in the actual soil and choose useful watering thresholds there.

USB-powered firmware reported about 4.03-4.04 V at the BAT/divider node. That is
not a calibrated cell-voltage measurement with the battery absent. Compare a
meter and firmware on battery-only power before changing `BATT_TRIM`. Battery
runtime, sleep current and outdoor sealing remain unverified.
