# Planter v2 — Power design decisions (allotment build)

Decision record for the multi-unit PCB revision. These units go to someone
else's allotment: **no serial console, no soldering iron, no tolerance for
battery babysitting.** Every decision below traces back to a failure mode we
actually hit on the fridgeReminder or planter v1 builds — references inline.

See [power-schematic.svg](power-schematic.svg) for the power section.

## Summary of decisions

| # | Decision | Replaces |
|---|----------|----------|
| 1 | **Protected cell** (PCM fitted) is a BOM requirement | unprotected pouch cells |
| 2 | **XIAO's own USB-C exposed at the enclosure edge** for charging | pigtails / external charge boards |
| 3 | **No J5019 / TP4056 / boost modules** anywhere in this design | — |
| 4 | **One high-side P-FET** gates a switched peripheral rail (sensor + panel) | GPIO-powered sensor, always-on panel |
| 5 | Battery divider **220k/220k with 100 nF cap fitted** | cap-less divider |
| 6 | Firmware ports the **fridge brownout-conservation logic** | none (v1 has no low-battery behaviour) |
| 7 | Radio stays **event-driven**: alerts + daily heartbeat only | — (v1 already correct; now policy) |
| 8 | **B/W panel**, redrawn only when the image changes | B/W panel redrawn every wake |

## 1. Battery: protected cell, ≥500 mAh

The XIAO ESP32-S3's onboard power path **has no low-voltage load
disconnect** — it will drag a flat cell toward 0 V. fridgeReminder killed a
deployed cell exactly this way (brownout spiral; see that repo's
`hardware.md`). A LiPo taken below ~2.5 V is permanently damaged: capacity
drops, internal resistance rises, and it then sags under WiFi spikes — which
presents as "the battery never lasts" on every cell that's been through it.

**Every cell ordered for this build must have a protection PCM**
(DW01A + FS8205 class: ~2.5 V undervoltage cutoff, overcharge, short-circuit).
It's the strip folded under the cell's yellow tape; listings say "with
protection circuit / PCM". Costs pennies.

The 2.5 V cutoff is the airbag, not the brakes — a cell spending time below
~3.0 V still degrades. Routine protection is firmware (§6). Size **≥500 mAh**:
smaller cells sag under WiFi connect current as they age (fridge lesson), and
at this node's ~2–3 mAh/day budget a 500 mAh cell is months of runtime anyway.

## 2. Charging: the XIAO's own USB-C, exposed at the enclosure edge

fridgeReminder found the hard way that USB pigtails silently break charging:

- A **USB-C charger port supplies 0 V** until it sees 5.1 kΩ CC pulldowns.
  Cheap pigtails don't have them → modern chargers never turn on.
- A **USB-A smart port** with D+/D− snipped gets no BC1.2 signalling and may
  cap at 100–500 mA.

The XIAO's own connector has proper CC resistors, so any charger works. The
enclosure therefore exposes the **XIAO's USB-C at the case edge** — design the
shell around this. No pigtail, no external charge board, nothing to explain.

Note the onboard charge IC runs at **~100 mA**: a flat 500 mAh cell needs
~5–6 h. Fine for a monthly "leave it plugged in overnight" routine; tell the
user short top-ups don't fill it.

## 3. Why not the J5019 boards in the drawer

Tempting (USB-C in, charges a cell, adjustable output) but wrong three ways:

1. **No protection.** It's a TP4056 *without* the DW01A/FS8205 pair — charge
   management only. The exact gap that killed the fridge cell.
2. **Boost quiescent draw.** The MT3608-class boost idles at ~100 µA–1 mA
   even unloaded (feedback divider + switcher). This node sleeps at ~35 µA
   total; the module would multiply the baseline by 10–50×.
3. **Redundant.** The XIAO already has the charger and power path.

Keep them for **HydraPlant's 12 V solenoid rail** — 3.7 V → 12 V adjustable
boost is precisely that job.

## 4. Peripheral power: one high-side P-FET, switched rail

v1 powers the moisture sensor from GPIO6 directly (works: ~5 mA against a
~40 mA pin limit, but it's living rough) and leaves the e-paper VCC hard-wired
to 3V3, relying on `hibernate()` + holding all five SPI lines through sleep.
fridgeReminder showed how fragile the hold approach is — it held only 2 of 5
lines, and floating CMOS inputs draw shoot-through current on the panel board.

v2 replaces both with **one AO3401 P-FET (Q1) high-side switching a
`3V3_SW` rail** feeding sensor VCC *and* panel VCC. Both loads are only
needed while awake, so one switch and one GPIO (keep GPIO6) cover both.
Sleep current questions about the panel simply cease to exist: unpowered
means nothing floats, nothing leaks, nothing to hold.

Circuit (see schematic): Q1 source → 3V3, drain → 3V3_SW, gate → GPIO6 with a
**100 k pull-up (R1) to 3V3**. R1 keeps Q1 off while the GPIO floats during
boot and deep sleep.

Firmware changes:

- **Logic inverts**: gate **LOW = rail ON** (v1's `SENSOR_PWR HIGH = on`).
- Wake sequence: GPIO6 LOW → 100 ms settle → read sensor → init + refresh
  panel → `display.hibernate()` → GPIO6 HIGH.
- **Before sleep, drive every 3V3_SW-side signal line LOW and
  `gpio_hold_en()` it** (CS, DC, RST, SCLK, MOSI — and note the sensor's AOUT
  is an input, nothing to do there). A line held HIGH into an unpowered
  peripheral back-powers the dead rail through the peripheral's ESD diodes.
  LOW, not HIGH, is the safe parked state once VCC is gated — the opposite
  of v1, where the panel stayed powered and lines parked HIGH.
- E-paper retains its image unpowered, so the display still shows the last
  reading all through sleep. No behaviour change for the user.

## 5. Battery sense: 220k/220k + 100 nF, cap fitted

The two repos currently disagree: planter v1's `NEW_HARDWARE.md` calls the
tap capacitor optional at ~110 kΩ source impedance; fridgeReminder's
`hardware.md` found it **mandatory** at 50 kΩ — without it readings sat
100–300 mV low and the 16-sample average made the droop *worse*, false-tripping
low-battery mode. The fridge doc has the better physics: the ADC
sample-and-hold pulls charge from the tap on every sample, and averaging just
repeats the theft. On a PCB the cap costs nothing.

**v2: 220k top / 220k bottom off B+, 100 nF tap → GND, tap → GPIO8.**
9.5 µA continuous. Keep `readBatteryVolts()`'s discard-first-sample habit,
and the single-point `BATT_TRIM` DMM calibration per unit.

## 6. Firmware: port the fridge brownout-conservation logic

planter v1 has **no** low-battery behaviour — it will cheerfully fire a WiFi
alert on a sagging cell, which is precisely the brownout spiral. The fridge
firmware already solved this; port it as-is:

- Read VBAT at boot, before WiFi (near open-circuit).
- Below **3.50 V**: skip WiFi, show a low-battery notice once, long naps.
- **Any brownout reset counts as low battery** regardless of the reading —
  a sagging cell recovers open-circuit voltage within seconds and passes the
  check, browns out under WiFi, and loops. Trusting the brownout breaks
  the loop.
- Hysteresis: resume normal operation only above **3.65 V**.
- Keep the NVS diagnostics (`brownouts`, `minVbatMv`): with no serial console
  in the field, they're the only post-mortem. Plug into USB later and the
  next boot prints them.

Layered with §1, the failure ladder is: firmware conserves at 3.5 V →
user sees "charge me" on the panel → if ignored for weeks, the PCM
disconnects at 2.5 V before the cell is harmed. No 0 V deaths possible.

## 7. Radio policy

The single biggest reason planter v1 lasts months where the fridge lasts
weeks: **47 of every 48 wakes never touch the radio.** WiFi + (on the fridge)
TLS every wake is a 5–10× daily-budget multiplier before anything else goes
wrong. v2 keeps the v1 model as explicit policy: connect only on
**alert or daily heartbeat**, single attempt, hard timeout, sleep regardless
of outcome.

## 8. Panel: B/W, and redrawn only when the image changes

The panel is the B/W GDEH0154D67 (SSD1681), 200x200. A tri-colour B/W/R module
was fitted briefly when a v1 panel died of ribbon damage, but the build is back
on B/W parts and we hold spares, so B/W is the standing choice rather than a
fallback. The tri-colour driver options remain in firmware (`PANEL` at the top
of `planter_v2.cpp`) because the modules are pin- and size-identical — the swap
is a driver class and nothing else — but `PANEL 0` is the default and the only
configuration this budget describes.

Refresh time is why. From the GxEPD2 panel constants:

| Panel | Controller | Full refresh | Est. energy per redraw |
|-------|-----------|--------------|------------------------|
| GDEH0154D67 (B/W) — **fitted** | SSD1681 | 2.6 s | ~0.03 mAh |
| GDEH0154Z90 (B/W/R) | SSD1682 | 14.0 s | ~0.16 mAh |
| GDEW0154Z04 (B/W/R, older) | IL0376F | 7.5 s | ~0.09 mAh |

Energy assumes ~40 mA drawn for the whole refresh window: the MCU stays fully
awake polling BUSY (GxEPD2 spins on `delay(1)`, there is no sleep-while-waiting
path) and the panel adds a few mA on top. **That 40 mA is the dominant
uncertainty in this whole budget — measure it on the first article.**

At 2.6 s the panel is no longer a threat to the budget: redrawing on every one
of the 48 wakes would cost 48 × 0.03 ≈ **1.4 mAh/day**. That is affordable,
where the same policy on the tri-colour part would have cost 7.7 mAh/day and
swamped everything else in the design put together.

The redraw stays conditional (`REDRAW_ONLY_ON_CHANGE`) anyway — it still cuts
roughly 1.2 mAh/day, about 45 % of the total budget, for no hardware cost. Be
clear about the change in status though: on the tri-colour panel this logic was
load-bearing, the one thing standing between the design and a dead cell. On B/W
it is an ordinary optimisation. If it ever misbehaves in the field, turning it
off is now a safe diagnostic step rather than a budget emergency.

E-paper holds its image with the rail gated off, so a refresh that changes
nothing buys nothing. Redraw when, and only when:

- moisture has moved ≥3 percentage points, or crossed a status-band boundary;
- the alert, battery-suspect, or conservation state has changed;
- it's the daily heartbeat wake — a forced full cycle every 24 h, which also
  keeps ghosting from accumulating;
- RTC memory is cold (first boot after the cell is connected).

While conserving, the screen already says "Charge me!" and the moisture figure
behind it is untrusted (§6), so no reading justifies the refresh — the state is
held until the battery state itself changes.

Colour policy: on the fitted B/W panel `EPD_ACCENT` folds to black, so the
layout renders in a single ink and nothing is lost — the accent was never the
only marker of any state. The layout keeps its accent/information split (red on
large low-detail elements only: alert frame, plant, big digits; fine strokes
always black) so that building for `PANEL 1` or `2` still produces a sane
screen, but on this hardware that distinction is inert.

## Power budget (paper numbers — measure on first article)

| Item | Draw |
|------|------|
| Deep sleep (XIAO) | ~14 µA (datasheet; measure) |
| Divider | 9.5 µA |
| 3V3_SW rail asleep | 0 |
| 48 wakes/day (boot + sensor read, no panel, no radio) | ~0.5 mAh/day |
| Panel refresh, B/W, ~6 redraws/day (§8) | ~0.2 mAh/day |
| 1 heartbeat + occasional alert (WiFi/MQTT) | ~0.2 mAh/day |
| **Total** | **~1.4 mAh/day** |

500 mAh protected cell at 80 % usable ≈ **9 months** between charges. The panel
is now a minor line item rather than the dominant one; the 48 daily wakes cost
more than the screen does.

The redraw count is worth watching in the field but is no longer critical: 6/day
is a guess for a settled pot, and a pot that oscillates across a band boundary
every wake would push the panel to ~1.4 mAh/day — roughly doubling the total to
~2.6 mAh/day and giving ~5 months, which is inconvenient rather than dangerous.
Widening `REDRAW_DELTA` is still the fix if it happens.
Verify deep-sleep current with a µA meter on the first assembled board:
~25 µA says the design works; hundreds of µA says a signal line is
back-powering the switched rail (§4).

## BOM (power section)

| Ref | Part | Value / spec | Notes |
|-----|------|--------------|-------|
| BT1 | 1S LiPo pouch, **protected** | ≥500 mAh, PCM fitted | non-negotiable |
| J1 | JST-PH 2.0 socket | 2-pin | check pigtail polarity — no standard |
| Q1 | AO3401 | P-FET, SOT-23, logic-level | Si2301 / DMG3415 equivalent |
| R1 | Resistor | 100 kΩ | Q1 gate pull-up to 3V3 |
| R2, R3 | Resistor, 1 % | 220 kΩ | divider off B+ |
| C1 | Ceramic cap | 100 nF | divider tap → GND, **fitted** |
| U1 | Seeed XIAO ESP32-S3 | — | USB-C exposed at enclosure edge |
