# Planter — Wiring Reference

## Board: Seeed XIAO ESP32-S3

## Moisture Sensor (Capacitive)

| Signal | Cable | XIAO Pin | GPIO |
|--------|-------|----------|------|
| AOUT   | yellow | D0/A0   | 1    |
| VCC    | red   | **D5**   | 6    |
| GND    | black | GND      | —    |

VCC is **switched, not tied to 3V3**: `readMoisturePercent()` drives D5 high, settles
100 ms, averages 16 ADC samples, then drives it low again — so the probe is only powered
~250 ms per 30-minute wake. That keeps it off the battery budget (a probe left on 3V3
draws ~5 mA against a board that otherwise sleeps at ~100 µA) and stops the electrodes
electrolysing in damp soil.

Do **not** run the sensor from 5V. The XIAO's 5V pin is fed from USB VBUS, so it is dead
on battery; and the sensor is ratiometric to its supply while the ESP32-S3 ADC is not, so
5V both invalidates the `MOISTURE_DRY`/`MOISTURE_WET` calibration and can push AOUT past
the 3.6 V pin maximum when dry. See [NEW_HARDWARE.md](NEW_HARDWARE.md).

Note D5 (GPIO6) is not D4 (GPIO5) — D4 is `EPD_BUSY`.

## E-Ink Display (Waveshare 1.54" B/W, SSD1681)

| Signal | XIAO Pin | GPIO |
|--------|----------|------|
| CS     | D1       | 2    |
| DC     | D2       | 3    |
| RST    | D3       | 4    |
| BUSY   | D4       | 5    |
| CLK    | D8       | 7    |
| DIN    | D10      | 9    |
| VCC    | 3V3      | —    |
| GND    | GND      | —    |
