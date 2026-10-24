# Planter — Wiring Reference

## Board: Seeed XIAO ESP32-S3

### Physical pad layout — read this before wiring

USB-C at the top, component side up:

```
                    ┌──────  USB-C  ──────┐
  Moisture AOUT ──  │ D0  (GPIO1)   5V    │  ── (unused)
        EPD CS  ──  │ D1  (GPIO2)   GND   │  ── EPD GND + sensor GND
        EPD DC  ──  │ D2  (GPIO3)   3V3   │  ── EPD VCC
       EPD RST  ──  │ D3  (GPIO4)   D10   │  ── EPD DIN     (GPIO9)
      EPD BUSY  ──  │ D4  (GPIO5)   D9    │  ── BATT sense  (GPIO8)
  Moisture VCC  ──  │ D5  (GPIO6)   D8    │  ── EPD CLK     (GPIO7)
        (free)  ──  │ D6  (GPIO43)  D7    │  ── (free)      (GPIO44)
                    └─────────────────────┘
```

**The right edge is where miswiring happens.** Counting down from the top you
pass **three power pads (5V, GND, 3V3) before any D pad**, and the D pads then
run *backwards*: D10, D9, D8, D7. Nothing on that edge is sequential.

Three traps, all of which have cost real bench time:

1. **DIN and CLK are not adjacent.** DIN is D10, CLK is D8, and **D9 sits
   between them carrying battery sense**. Wire DIN one pad low and it lands on
   D9: no data reaches the panel, `BUSY` never asserts, and the panel keeps its
   factory image. The firmware reports `Display updated.` regardless — see the
   `_Update_Full` note below.
2. **CLK is the 6th pad down the right edge, not the 8th.** The "D8" label says
   nothing about position.
3. **D5 is sensor power, not a display signal**, and sits directly below `BUSY`
   on D4. Wire the display one pad low here and the *sensor* breaks too, which
   is a useful cross-check: a working moisture reading proves D0 and D5 are
   right and confines any fault to the display group.

Full allocation, all 11 GPIOs:

| XIAO | GPIO | ADC | Signal |
|------|------|-----|--------|
| D0 | 1 | ADC1_CH0 | Moisture AOUT |
| D1 | 2 | — | EPD CS |
| D2 | 3 | — | EPD DC |
| D3 | 4 | — | EPD RST |
| D4 | 5 | — | EPD BUSY |
| D5 | 6 | — | Moisture VCC (switched) |
| D6 | 43 | — | free (UART0 TX) |
| D7 | 44 | — | free (UART0 RX) |
| D8 | 7 | — | EPD CLK |
| D9 | 8 | ADC1_CH7 | BATT sense |
| D10 | 9 | — | EPD DIN |

Nine of eleven pins used. D9/GPIO8 is the **only** remaining ADC-capable pin —
GPIO1–9 are all ADC1 on this part and everything else is taken.

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

## E-Ink Display (Waveshare 1.54" B/W, SSD1681, 200x200)

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

The fitted panel is the **B/W GDEH0154D67 (SSD1681)**. A panel was lost to
ribbon damage once and swapped for a tri-colour B/W/R module as a stopgap; the
build is back on B/W parts, which we stock, so treat B/W as the standing
configuration.

Driver selection is `PANEL` at the top of [src/main.cpp](src/main.cpp). All
three modules are 200x200 and share this 8-pin interface, so switching between
them is **a firmware driver swap only — no wiring change**.

| `PANEL` | Part | Controller | Refresh |
|---------|------|-----------|---------|
| 0 | GDEH0154D67 (B/W) — **fitted, default** | SSD1681 | 2.6 s |
| 1 | GDEH0154Z90 (B/W/R) | SSD1682 | 14 s |
| 2 | GDEW0154Z04 (B/W/R, older module) | IL0376F | 7.5 s |

On `PANEL 0` the red accent constant folds to black, so the layout renders in a
single ink with nothing lost. If you ever do fit a tri-colour module and it
shows nothing or garbage on `PANEL 1`, try `PANEL 2` — Waveshare has shipped
both tri-colour controllers under the same product name.

The firmware redraws only on change; at 2.6 s that is an optimisation rather
than a necessity. See
[power-design.md](power-design.md) §8.

### MISO is deliberately unassigned — do not "tidy" this

[src/main.cpp](src/main.cpp) brings up SPI as:

```cpp
SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);   // the -1 is load-bearing
```

That `-1` disables MISO on purpose. The XIAO ESP32-S3's **default hardware MISO
is GPIO8 — which is D9, the battery sense pin**. Let `SPI.begin()` take its
defaults and it claims GPIO8 for SPI, silently breaking battery monitoring while
the display carries on working. E-paper is write-only, so MISO is never needed.

### Diagnosing a dead panel

`display.init(115200, ...)` enables GxEPD2 diagnostics, which print the BUSY
wait time per refresh:

```
_Update_Full : 2600      ← healthy: the panel actually refreshed
_Update_Full : 3         ← BUSY never asserted
```

A single-digit figure means GxEPD2 issued the refresh, saw BUSY already low,
concluded it had finished, and returned — then `hibernate()` cut the panel off
mid-cycle. It prints `Display updated.` either way, so **`Display updated.` is
not evidence the screen changed.** The `_Update_Full` number is.

Because a powered, idle SSD1681 holds BUSY low and so does a disconnected pin, a
stuck-low BUSY cannot be told from idle with a meter — it only reveals itself
during a refresh. If BUSY is confirmed continuous to D4 and the panel has 3V3,
the fault is upstream in the command path (CS/DC/RST/CLK/DIN), not in BUSY.
