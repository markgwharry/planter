# Planter — New Hardware: 1S LiPo Battery Monitor

Addendum to [WIRING.md](WIRING.md). Defines a battery-voltage sense channel for the
Seeed XIAO ESP32-S3 so the firmware can report state-of-charge and, critically,
flag when the cell has sagged far enough to corrupt the moisture reading.

## Why we need it

The moisture sensor is **ratiometric to its supply** — its analogue output scales
with VCC. The ESP32-S3 ADC, by contrast, references a fixed internal band-gap, so it
does **not** compensate. The sensor is powered from the XIAO's `3V3` rail, which is the
output of the onboard LDO (an AP2112K-class part, dropout ≈ 250 mV at light load).

| Cell voltage | 3V3 rail | Effect on moisture reading |
|---|---|---|
| 4.20 → ~3.60 V | regulated 3.30 V | stable — calibration valid |
| < ~3.55 V (LDO dropout) | **sags below 3.30 V** | sensor output droops → reads **falsely dry** |

So the moisture calibration in [main.cpp:26-27](src/main.cpp#L26-L27) only holds while the
cell is above ~3.6 V. Below that the planter will under-report moisture and could miss a
genuine dry-out. We have no way to see this happening today — there is **no onboard
battery-voltage tap** on the XIAO ESP32-S3. This mod adds one.

## Measurement principle

A 1S LiPo spans **3.0 V (empty) → 4.2 V (full)**. The ESP32-S3 ADC usable input is
~0–3.1 V, so 4.2 V would over-range (and exceed the pin's 3.6 V absolute maximum). A
**2:1 resistive divider** halves it: 4.2 V → 2.10 V at the tap, comfortably inside range
and below the pin limit even when the divider floats.

Read the tap with `analogReadMilliVolts()` (applies the factory eFuse ADC calibration,
handling the S3's ADC non-linearity), then multiply by the divider ratio:

```
V_batt = analogReadMilliVolts(BATT_PIN) × 2   (mV)
```

## Bill of materials

| Ref | Part | Value | Notes |
|---|---|---|---|
| R1 | Resistor, 1% metal film | 220 kΩ | divider top (B+ → tap) |
| R2 | Resistor, 1% metal film | 220 kΩ | divider bottom (tap → GND) |
| C1 | Ceramic capacitor | 100 nF | tap → GND; buffers the ADC sample-and-hold |

Continuous divider current = 4.2 V / 440 kΩ ≈ **9.5 µA**, negligible against the XIAO's
own deep-sleep draw (~50–150 µA in practice). Source impedance at the tap is ~110 kΩ;
C1 supplies the ADC's sample-and-hold charge so this stays accurate.

## Schematic

```
   B+ (LiPo +, XIAO bottom pad)
     │
    [R1] 220k
     │
     ├───────────────► BATT_PIN  (D9 / GPIO8, ADC1_CH7)
     │
    [C1] 100nF
     │
    [R2] 220k
     │
   GND (B- / XIAO GND)
```

## Pin assignment

`D9 / GPIO8` is the only free ADC-capable pin — everything else is taken by the moisture
sensor and the e-ink display.

| Signal | XIAO Pin | GPIO | ADC channel |
|--------|----------|------|-------------|
| BATT sense (divider tap) | D9 | 8 | ADC1_CH7 |

Tap **B+** and **B-** from the LiPo solder pads on the underside of the XIAO (the same
pads the cell connects to). No cut to the existing moisture/display wiring is required.

## Firmware hooks

Add to [src/main.cpp](src/main.cpp):

```cpp
// ── Battery monitor ─────────────────────────────────────────────
#define BATT_PIN     8     // D9 / GPIO8, ADC1_CH7
#define BATT_DIV     2.0f  // 220k / 220k divider
#define BATT_TRIM    1.0f  // fine-trim for resistor tolerance (see below)
#define BATT_LOW     3.60f // warn: at/below LDO dropout, moisture suspect
#define BATT_CRIT    3.45f // critical: recharge now

float readBatteryVolts() {
    analogReadResolution(12);
    uint32_t mv = 0;
    analogReadMilliVolts(BATT_PIN);          // discard first (settling)
    for (int i = 0; i < 8; i++) { mv += analogReadMilliVolts(BATT_PIN); delay(2); }
    return (mv / 8.0f) * BATT_DIV * BATT_TRIM / 1000.0f;
}
```

Integration points:
- Call `readBatteryVolts()` in `setup()` alongside the moisture read.
- Add `"batt":%.2f` and a `"batt_low":true/false` flag to the MQTT payload in
  `publishMQTT()` so the TIG stack can chart it.
- Treat `V_batt <= BATT_LOW` as an alert condition (reuse the existing alert path) — at
  that point the moisture figure is no longer trustworthy.
- Optionally show a small battery glyph / voltage on the e-ink in `updateDisplay()`.

### Approximate 1S LiPo state-of-charge

The discharge curve is non-linear; report the voltage and use this only as a coarse gauge:

| V_batt | SoC |
|---|---|
| 4.20 | 100% |
| 3.90 | 75% |
| 3.80 | 60% |
| 3.70 | 40% |
| 3.60 | 20% |
| 3.50 | 10% |
| 3.30 | 0% (empty) |

## Trim procedure

Resistor tolerance and ADC offset introduce a small error. Once built:

1. Measure the cell with a DMM at the B+ pad.
2. Read `readBatteryVolts()` over serial (`DEBUG_MODE true`).
3. Set `BATT_TRIM = V_dmm / V_reported` and reflash.

A single-point trim at ~3.8 V is sufficient for this application.

## Alternatives considered

| Option | Current | Trade-off |
|---|---|---|
| **220k/220k, always-on** (chosen) | 9.5 µA | Lower source impedance → best ADC accuracy; current negligible vs board sleep draw. |
| 1M/1M, always-on | 2.1 µA | Lower drain, but 500 kΩ source impedance is marginal for the ADC — relies more heavily on C1 and slow sampling. |
| Switched divider (P-MOSFET high-side) | ~0 µA idle | Lowest drain, but adds a MOSFET + gate drive. A **low-side** GPIO switch is *not* safe here: it would float the ADC pin to B+ (up to 4.2 V) when idle, exceeding the 3.6 V pin maximum. |

For a planter on a 30-minute duty cycle the always-on 220k/220k divider is the pragmatic
choice — the 9.5 µA is lost in the noise of the XIAO's own sleep current.
