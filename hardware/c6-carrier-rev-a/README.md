# Planter XIAO ESP32-C6 carrier - Rev A

Status: schematic-level design, ready for KiCad capture after the exact battery
protection board and mechanical arrangement are photographed and measured.

This carrier replaces Dupont wiring while retaining the XIAO as the radio,
charger, regulator and processor module. It does not reproduce the XIAO's USB,
charger or RF circuitry.

## Electrical architecture

```text
1S cell B+/B- -> protection PCB -> protected P+/P-
                                      |
                                      +-> J1 -> JP1 -> XIAO BAT+/BAT-
                                      |
                                      +-> R2 200k -> D0/A0 battery ADC

XIAO 3V3 -> Q1 AO3401A -> 3V3_SW -> moisture sensor + e-paper module
```

The protection board must be a common-port 1S design: both charge and load use
P+/P-. B- must never be joined to carrier ground because that bypasses the
protection switch. A pack that already contains protection must not receive a
second protection PCB.

The XIAO's own USB-C remains the only charger. Charging-cover access is an
enclosure requirement but is deliberately outside this electrical revision.

## Fixed pin map

| XIAO label | ESP32-C6 GPIO | Carrier signal | Direction |
|---|---:|---|---|
| D0 / A0 | 0 | VBAT_ADC | input |
| D1 / A1 | 1 | MOIST_ADC | input |
| D2 / A2 | 2 | PERIPH_EN_N | output, LOW = on |
| D3 | 21 | EPD_CS | output |
| D4 | 22 | EPD_DC | output |
| D5 | 23 | EPD_RST | output |
| D6 | 16 | EPD_BUSY | input |
| D7 | 17 | spare / test pad | - |
| D8 | 19 | EPD_SCLK | output |
| D9 | 20 | spare; MISO unused | - |
| D10 | 18 | EPD_MOSI | output |

The display is write-only. Firmware must initialise SPI with MISO disabled.

## Battery input and measurement

J1 accepts protected P+ and P-. JP1 is a normally-fitted removable link in the
positive lead so complete-device sleep and wake current can be measured without
cutting a trace.

For the current XIAO ESP32-C6 revision, the carrier fits R2 = 200k from protected
P+ to D0/A0. The XIAO schematic already has R10 = 200k from A0 to ground, giving
a 1:2 divider. C1 = 100nF is fitted from A0 to ground at the XIAO.

R3 is a DNP 200k bottom-resistor footprint. It is populated only if continuity
testing proves a future XIAO revision no longer contains the onboard 200k leg.
Never populate R3 on the current revision: it would change the divider ratio.

## Switched peripheral rail

Q1 is an AO3401A P-channel MOSFET:

- source: XIAO 3V3;
- drain: 3V3_SW;
- gate: R4 1k from D2;
- R1 100k from gate to 3V3, holding the rail off during reset and sleep.

C2 = 10uF and C3 = 100nF decouple 3V3_SW beside the display connector. The
sensor and display are powered only while awake. Before Q1 is switched off,
firmware drives CS, DC, RST, SCLK and MOSI LOW and gives BUSY a pull-down so an
unpowered module cannot be back-powered through its signal pins.

ESP32-C6 retains each selected pad with `gpio_hold_en()` directly during deep
sleep. Unlike the S3 build, it must not call the older global
`gpio_deep_sleep_hold_en()` API, which current ESP-IDF does not expose for C6.

## Connectors

### J2 - moisture probe, keyed 3-way

| Pin | Signal |
|---:|---|
| 1 | 3V3_SW |
| 2 | MOIST_OUT |
| 3 | GND |

R5 = 1k sits between MOIST_OUT and D1/A1. C4 = 100nF from D1/A1 to ground is
fitted beside the XIAO. An optional low-leakage ESD diode footprint may be added
after the actual cable length and connector are known.

### J3 - WeAct 1.54-inch module, 2x4 at 2.54mm pitch

The supplied WeAct mechanical drawing defines the module header as:

| Pin | WeAct signal | Carrier connection |
|---:|---|---|
| 1 | BUSY | D6 / GPIO16 |
| 2 | RES | D5 / GPIO23 |
| 3 | D/C | D4 / GPIO22 |
| 4 | CS | D3 / GPIO21 |
| 5 | SCL | D8 / GPIO19 |
| 6 | SDA | D10 / GPIO18 |
| 7 | GND | GND |
| 8 | VCC | 3V3_SW |

Use a shrouded, polarised 2x4 header on the carrier if the display is cabled.
Pin 1 must be marked on copper and silkscreen. The module accepts 3.3-5V power
but its I/O is 3.3V, so this carrier powers it from 3V3_SW.

## Mechanical capture inputs

From the supplied WeAct board-shape PDF:

- overall module envelope: 50.12 x 32.60mm;
- mounting-hole centres: 44.52 x 27.00mm;
- four mounting holes: 3.00mm diameter;
- nominal corner radius: 1.50mm;
- 2x4 signal header: 2.54mm pitch, on the left-hand side when viewed from the
  component face.

Use the supplied STEP model for enclosure clearance and connector orientation.

## Layout requirements

- Use Seeed's official XIAO ESP32-C6 footprint and antenna keepout.
- No copper, battery, display backplane or metal fastener beneath/in front of
  the XIAO antenna end.
- The preferred field assembly solders the XIAO directly to the carrier and
  intentionally reflows the underside BAT pads.
- Keep USB-C, BOOT and RESET reachable with the enclosure cover removed.
- Put J1 and J2 on the downward/cable side; put J3 toward the display cavity.
- Add labelled test points: PACK+, GND, 3V3, 3V3_SW, VBAT_ADC, MOIST_ADC,
  PERIPH_EN_N, EPD_BUSY, D7 and D9.
- Mark board name, `REV A`, connector polarity and pin 1 on both copper and
  silkscreen where practical.

## First-article gates

Do not call the board field-ready until all of these pass:

1. Confirm the protection PCB's exact B+/B-/P+/P- arrangement before connection.
2. With USB removed, A0 measures half of P+ to P- within resistor tolerance.
3. Calibrated firmware battery voltage agrees with a DMM within 50mV.
4. USB-C charges through the protection board without abnormal heating.
5. Sensor and display operate from 3V3_SW and fully lose power when it is off.
6. No signal pin remains high while 3V3_SW is off.
7. Measure whole-assembly deep-sleep current at JP1; investigate any result in
   the hundreds of microamps before outdoor deployment.
8. Recalibrate dry and wet moisture endpoints on the assembled carrier.
9. Run a 24-hour bench soak including repeated Wi-Fi and display updates.
10. Complete enclosure gasket, display-window and charging-access tests.

## Files

- `bom.csv`: initial electrical BOM and DNP/configuration parts.
- `carrier-schematic.svg`: review drawing of the complete power and I/O path.
- `netlist.csv`: connection-level capture checklist for KiCad.
- `weact-module-mechanics.md`: source dimensions and orientation notes.
- `../../platformio-c6.ini`: C6 firmware build configuration.

Build the shared v2 firmware for this carrier with:

```sh
pio run --project-conf platformio-c6.ini -e esp32c6_rev_a
```

The C6 build intentionally pins pioarduino platform 55.03.311 (Arduino-ESP32
3.3.11) rather than following its moving `stable` URL. The official PlatformIO
Espressif platform currently declares the XIAO ESP32-C6 as ESP-IDF-only even
though Arduino-ESP32 itself supports the C6.

The original S3 v2 proposal remains buildable from the same source using
`platformio-s3-v2.ini`; this prevents the C6 pin map from silently replacing the
existing design.
