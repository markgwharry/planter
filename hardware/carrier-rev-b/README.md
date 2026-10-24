# Planter XIAO carrier - Rev B (PCB, C6 or S3)

Rev A's circuit turned into a 2-layer PCB that sits directly behind the WeAct
1.54-inch e-paper module. It takes a **socketed** Seeed XIAO ESP32-C6 **or**
ESP32-S3; the firmware build picks the chip, the board doesn't care.

Status (2026-10-24): schematic and layout generated and checked. ERC clean,
DRC clean with **zero** violations, schematic parity clean, 0 unconnected.
Nothing has been fabricated, fitted or powered. First-article gates below.

| Top | Underside (display side) |
|---|---|
| ![top](render-top.png) | ![bottom](render-bottom.png) |

## What changed from Rev A

| Item | Rev A (perfboard P1) | Rev B | Why |
|---|---|---|---|
| XIAO mounting | Soldered | **2x7 2.54mm sockets/pins** | Swap C6/S3. Gives access to the underside BAT pads. Room for 0603s underneath. |
| Pin map | By C6 GPIO | **By XIAO pad label** | The castellations are common to C6 and S3. Arduino's `D0..D10` resolve per chip. |
| EPD BUSY | D6 | **D7** | D6 is UART0 TX on both chips. The ROM log drives it on every wake into the BUSY pin of an unpowered panel. D7 (RX) is an input from reset. |
| Battery to XIAO | Wires | **J4 wire pads** beside U1 | The underside BAT pads differ between C6 and S3, so no pads are copied into the footprint. |
| Test points | 10 | **3** (PACK+, GND, 3V3_SW) | Every other net is on a XIAO castellation, which you can probe while it's socketed. |
| Connectors | TBD | **JST-PH side-entry** S2B (battery) / S3B (probe) | Cables exit the bottom edge, next to the USB-C. |
| Display | Cabled 2x4 | **2x4 female socket on the underside** | The module's own header plugs straight in. |

Electrical values are unchanged: 200k/200k divider + 100 nF, AO3401A high-side
switch with 100k pull-up and 1k gate resistor, 1k + 100 nF on the probe input,
10 uF + 2 x 100 nF on 3V3_SW.

## Pin map (both chips)

| XIAO pad | Net | C6 GPIO | S3 GPIO |
|---|---|---:|---:|
| D0/A0 | VBAT_ADC | 0 | 1 |
| D1/A1 | MOIST_ADC | 1 | 2 |
| D2 | PERIPH_EN_N (LOW = on) | 2 | 3 |
| D3 | EPD_CS | 21 | 4 |
| D4 | EPD_DC | 22 | 5 |
| D5 | EPD_RST | 23 | 6 |
| D6 | spare (UART TX) | 16 | 43 |
| D7 | EPD_BUSY | 17 | 44 |
| D8 | EPD_SCLK | 19 | 7 |
| D9 | spare (S3 default MISO) | 20 | 8 |
| D10 | EPD_MOSI | 18 | 9 |

S3 D2 = GPIO3 is a JTAG-source strapping pin. It is only sampled if the eFuse
selects it, and R1 holds it high anyway.

## Mechanical stack

```
   XIAO (USB-C at bottom edge, antenna end up)  ── on 2x7 female sockets
   ─────────── Rev B carrier, 50.12 x 32.60 mm, 1.6 mm ───────────
      J3 2x4 female socket (underside) + 4 x M2.5 standoffs, ~11 mm
   ─────────── WeAct 1.54" module (header pins pointing back) ───────────
   ─────────── e-paper panel ───────────
```

- The outline, the M2.5 hole pattern (44.52 x 27.00 mm) and the right-edge FPC
  recess all copy the WeAct board-shape drawing, so the carrier sits exactly
  over the module.
- J3 pin 1 is at (1.93, 12.49) mm from the top-left corner in the top view,
  measured off the drawing. **Check it against a real module before ordering.**
- Standoff length = socket height + header plastic. 8.5 mm socket + 2.5 mm
  plastic ≈ 11 mm. Measure your parts.
- A low-profile XIAO (male pins soldered direct) still clears the 0603s
  underneath it. You lose the ability to swap chips.

## Antenna

The XIAO's antenna end (away from USB) sits over a copper keep-out on both
layers (no tracks, vias or plane), plus a no-plane strip under the first
~5 mm of the module. The C6's ceramic antenna is about 10 mm above the carrier
and about 20 mm above the panel. That should be fine indoors and at allotment
range, but it is **unmeasured**. The plain XIAO ESP32-S3 uses a U.FL and an
external antenna, so leave room in the enclosure for its cable and whip.

## Battery

Protected P+/P- go to J1 (pin 1 = P+). **JST-PH LiPo polarity is not
standardised: check every pigtail before plugging in.** JP1 is the
removable current-measurement link. PACK+ then goes to J4.1 and, by a short
strain-relieved wire, to the XIAO's underside BAT+. J4.2 goes to BAT-. The
external protection PCB stays with the cell, as in Rev A.

## Files

| File | Purpose |
|---|---|
| `netlist.csv` | Reviewed connectivity. `make_kicad.py` builds the schematic from it. |
| `make_kicad.py` | Writes `planter-carrier-rev-b.kicad_sch` and `Planter.kicad_sym`. |
| `make_pcb.py` | Writes the custom XIAO footprint and `planter-carrier-rev-b.kicad_pcb`. It takes nets from the schematic's exported netlist, so parity holds by construction. Needs KiCad's bundled Python. |
| `check.sh` | Regenerates everything. Fails on any ERC, DRC or parity finding. |
| `make_fab.sh` | Gerber/drill zip, top-side CPL and BOM into `build/fab/` (JLCPCB-shaped). |

Both generators are reproducible baselines. If you edit in the KiCad GUI,
commit before re-running them.

## Firmware

`platformio-rev-b.ini` at the repo root:

```
pio run -c platformio-rev-b.ini -e esp32c6_rev_b -t upload
pio run -c platformio-rev-b.ini -e esp32s3_rev_b -t upload
```

Both set `PLANTER_CARRIER_REV_B`, which selects the D-label pin map in
`firmware-v2/planter_v2.cpp`. The S3 moisture endpoints are placeholders
(the C6 P1 figures) until it is calibrated.

## First-article gates

Carried over from Rev A, plus Rev B items:

1. Before ordering: print the top silk 1:1 and lay it over a real WeAct module.
   Check the holes, the J3 pin 1 position and the FPC recess.
2. Check every battery pigtail's polarity against J1 (pin 1 = P+).
3. With USB removed: A0 reads half of PACK+ within resistor tolerance.
   Firmware battery voltage agrees with a DMM within 50 mV after `BATT_TRIM`.
4. 3V3_SW is fully off between wakes. No EPD line is high while it is off.
   Check BUSY (now D7) in particular.
5. Measure whole-assembly sleep current at JP1, separately for C6 and S3.
6. Recalibrate dry/wet endpoints on each chip.
7. Check Wi-Fi RSSI at the install site with the enclosure closed, C6 first.
8. 24-hour soak with repeated Wi-Fi and display updates, on each chip.
