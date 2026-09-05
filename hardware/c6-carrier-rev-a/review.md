# Electrical review - 2026-09-05

The initial package at 02cb06c contained an architectural SVG and a connection
checklist, not a native KiCad schematic. Review found two circuit errors and
several incomplete capture details. This revision corrects the electrical
specification but does not claim fabrication readiness or an ERC pass.

## Corrections

1. **Battery divider: fit R3.** The initial claim that the XIAO supplies the
   lower 200k leg was wrong. In Seeed's 2026-01-14 schematic, sheet 03 Power,
   R10 connects charger IREF to GND. GPIO0/A0 has no such resistor. Both external
   200k parts are mandatory; C1 is fitted. The firmware ratio stays 2.0.
2. **MOSFET pull-up: connect both R1 terminals.** The original CSV assigned
   R1.1 and Q1.G to the 3V3 net and omitted R1.2. It now connects R1.1 to 3V3
   and R1.2 to GATE. R4 connects GPIO2 to GATE. Q1 pin mapping is explicit.
3. **Current link:** place the divider after JP1 so the link measures/disconnects
   the entire battery-fed assembly. The original ASCII overview was ambiguous.
4. **Capture coverage:** added TP1-TP8, sensor connector bypass C5, and explicit
   VBUS no-connect in the drawing. Removed the unspecified DNP ESD part from the
   BOM; select it when the probe cable is known.
5. **Mechanics:** corrected the interpretation of the WeAct R1.5 and offset
   annotations. Direct XIAO reflow with underside BAT contact is now conditional
   on checking pad heights and underside component clearance.

## What is established

- Protected P+/P- supplies the XIAO BAT pads and the complete divider.
- Q1 high-side switching and the shared firmware's active-low enable agree.
- C6 pin allocation is unique and uses ADC-capable GPIO0/GPIO1 for the two inputs.
- WeAct header signal order agrees with the supplied module drawing.
- Firmware retains the separate S3 v2 pin allocation.
- Every fitted BOM electrical terminal appears in the capture netlist; the
  check also rejects any terminal assigned to two different nets.

## Still required before fabrication

- Native KiCad schematic, verified symbol/footprint pin mappings, and ERC.
- Exact J1/J2 connector parts and polarity, protection board identification,
  selected XIAO underside contact method, PCB outline and mounting holes.
- Confirm the display being fitted: the mechanical input is WeAct, while the
  existing firmware default is the Waveshare/GDEH0154D67 driver. The shared SPI
  interface does not establish controller/refresh compatibility.
- Final cable ESD decision and local decoupling placement.
- Layout and antenna clearance, DRC, enclosure fit, and a first-article bench run.
- Bench checks include battery ADC calibration, actual rail-off current,
  low-battery/brownout recovery, USB charging and the final dry/wet calibration.

The removable charging cover remains a recorded enclosure follow-up.

## Validation performed

- `python3 hardware/c6-carrier-rev-a/check_capture.py`: PASS, 64 terminals;
  deliberately reintroducing either missing R3 or the conflicting gate net fails.
- C6 build with `platformio-c6.ini`: PASS (49,724 bytes RAM; 1,071,606 bytes
  application flash). Uses pioarduino 55.03.311 / Arduino 3.3.11.
- S3 v2 regression build with `platformio-s3-v2.ini`: PASS (50,068 bytes RAM;
  742,369 bytes application flash).
- SVG rendered with Inkscape and visually checked in full; labels are readable.
- No hardware flashed and no KiCad ERC/DRC claimed.

## Primary evidence

- [Seeed XIAO ESP32-C6 schematic, 2026-01-14](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32C6/XIAO_ESP32_C6_v1.0_SCH_260114.pdf)
- [AOS AO3401A datasheet](https://www.aosmd.com/pdfs/datasheet/AO3401A.pdf)
- [Arduino XIAO ESP32C6 pin definitions](https://github.com/espressif/arduino-esp32/blob/master/variants/XIAO_ESP32C6/pins_arduino.h)
- User-supplied `WeAct-EpaperModule-1.54 Board Shape 外形.pdf`, pages 1-3,
  visually inspected in the preceding design pass.
