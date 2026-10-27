# Planter XIAO carrier - Rev B (PCB, C6 or S3)

Rev A's circuit turned into a 2-layer PCB that sits directly behind the WeAct
1.54-inch e-paper module. It takes a **socketed** Seeed XIAO ESP32-C6 **or**
ESP32-S3; the firmware build picks the chip, the board doesn't care.

Status (2026-10-27): on-board 1S protection added (DW01A + FS8205A), the
antenna keep-out removed (external antenna), LCSC numbers on every
machine-placeable part, and all silkscreen at or above JLCPCB's legend
minimums (1.0 mm text, 0.15 mm lines). ERC clean, DRC clean with **zero** violations
including warnings, schematic parity clean, 0 unconnected; the exported
netlist was checked pin by pin against `netlist.csv` and the two datasheets.
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
| Cell protection | External PCB on the cell | **On board: DW01A (U2) + FS8205A (Q2)** | J1 takes a bare or a protected cell. |
| Antenna | Over a copper keep-out | **No keep-out**; ground pour under the module | An external antenna is used (see Antenna). |

Electrical values are otherwise unchanged: 200k/200k divider + 100 nF, AO3401A
high-side switch with 100k pull-up and 1k gate resistor, 1k + 100 nF on the
probe input, 10 uF + 2 x 100 nF on 3V3_SW.

## Battery protection

Standard DW01A low-side circuit, values from the PUOLOP DW01A datasheet:

| Part | Value | Connection |
|---|---|---|
| U2 DW01A (C351410) | SOT-23-6 | GND to the cell negative (PACK_IN-), TD open |
| Q2 FS8205A (C2830320) | SOT-23-6, 1 S1, 2 D, 3 S2, 4 G2, 5 D, 6 G1 | FET1 (S1/G1) = discharge, S1 to PACK_IN-; FET2 (S2/G2) = charge, S2 to GND |
| R6 | 100R | PACK+ to U2 VCC. Fed after JP1 so U2's ~3 uA shows in the sleep measurement |
| C6 | 100 nF | U2 VCC to PACK_IN- |
| R7 | 1k | U2 VM/CS to GND |

Trips at 4.30 V over-charge, 2.50 V over-discharge, and 150 mV across Q2
(roughly 3 A). GND is the protected pack negative; PACK_IN- must never be
joined to it. The XIAO's own charger (C6 ~100 mA, S3 ~50 mA) charges through
Q2, so the over-charge cut-off backs it up. U2/Q2 sit along the bottom edge
under the XIAO's USB end (they are 1.1 mm tall; the module is on 8.5 mm
sockets). R6/C6/R7 sit between J1 and J4.

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

Both chips run from an **external U.FL antenna**, so the board has no antenna
keep-out and the ground pour runs under the whole module. Leave room in the
enclosure for the cable and whip.

The XIAO ESP32-C6 defaults to its built-in ceramic antenna, which now sits over
ground. Firmware must select the U.FL: drive GPIO3 LOW (enables the RF switch),
wait ~100 ms, then drive GPIO14 HIGH (external antenna). The plain XIAO
ESP32-S3 only has the U.FL.

## Battery

The cell goes to J1 (pin 1 = cell +, pin 2 = cell -); a bare or a protected
cell is fine. **JST-PH LiPo polarity is not standardised: check every pigtail
before plugging in.** JP1 is the removable current-measurement link. PACK+ then
goes to J4.1 and, by a short strain-relieved wire, to the XIAO's underside
BAT+. J4.2 (protected GND) goes to BAT-. On the C6 the BAT- pad is the one
near the D8 marking and BAT+ the one near D5.

Hand-fitted parts that are not in the JLCPCB BOM: the XIAO and its two 1x7
2.54 mm female sockets, J3 (2x4 2.54 mm female socket, underside), J4 and JP1
(1x2 2.54 mm headers) plus a jumper shunt for JP1, and four M2.5 standoffs
(~11 mm).

## Files

| File | Purpose |
|---|---|
| `netlist.csv` | Reviewed connectivity. `make_kicad.py` builds the schematic from it. |
| `make_kicad.py` | Writes `planter-carrier-rev-b.kicad_sch` and `Planter.kicad_sym`. |
| `make_pcb.py` | Writes the custom XIAO footprint and `planter-carrier-rev-b.kicad_pcb`. It takes nets from the schematic's exported netlist, so parity holds by construction. Needs KiCad's bundled Python. |
| `check.sh` | Regenerates everything. Fails on any ERC, DRC or parity finding. |
| `make_fab.sh` | Into `build/fab/`: Gerber/drill zip; `-bom.csv` and `-cpl-top.csv` for JLCPCB assembly (only parts with an LCSC number, every designator listed in full); `-bom-full.csv` with everything, including the hand-fitted parts. JLCPCB centres each part on its CPL coordinate, so through-hole parts (J1, J2) are written at the centre of their pins, not KiCad's pin-1 anchor. Check JLCPCB's placement preview: SOT-23 and SOT-23-6 rotations sometimes need correcting there. |

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
2. Check every battery pigtail's polarity against J1 (pin 1 = cell +).
   Before the first cell: power J1 from a current-limited bench supply in
   place of the cell. Check that GND follows the supply negative at 3.7 V,
   that the output cuts off as the supply drops below ~2.5 V, and that it
   comes back once the supply is raised above ~2.9 V.
3. With USB removed: A0 reads half of PACK+ within resistor tolerance.
   Firmware battery voltage agrees with a DMM within 50 mV after `BATT_TRIM`.
4. 3V3_SW is fully off between wakes. No EPD line is high while it is off.
   Check BUSY (now D7) in particular.
5. Measure whole-assembly sleep current at JP1, separately for C6 and S3.
   Use a low-burden meter (e.g. a PPK2): U2 is powered after JP1, so a
   high-burden uA range can drop its supply enough to trip it under load.
   Don't ground-reference the cell or J1.2 while USB is on an earthed PC:
   that bypasses Q2.
6. Recalibrate dry/wet endpoints on each chip.
7. Check Wi-Fi RSSI at the install site with the enclosure closed, C6 first,
   with the external antenna selected (GPIO3 LOW, GPIO14 HIGH).
8. 24-hour soak with repeated Wi-Fi and display updates, on each chip.
