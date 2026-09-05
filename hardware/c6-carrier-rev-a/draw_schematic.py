"""Render the reviewed circuit drawing; matching net labels are connected.

This is an SVG review artifact, not a KiCad schematic or ERC substitute.
Run from any directory: python3 draw_schematic.py
"""
from pathlib import Path
from html import escape

parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1160" viewBox="0 0 1600 1160">',
         '<style>text{font-family:Arial,sans-serif;fill:#172b3a} .wire{stroke:#216351;stroke-width:2;fill:none}</style>',
         '<rect width="1600" height="1160" fill="#f4f7f6"/>']


def text(x, y, s, size=16, bold=False):
    parts.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{700 if bold else 400}">{escape(s)}</text>')


def line(x1, y1, x2, y2):
    parts.append(f'<path class="wire" d="M{x1},{y1} L{x2},{y2}"/>')


def box(x, y, w, h, title):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="white" stroke="#bacbc4"/>')
    text(x+20, y+32, title, 20, True)


def resistor(x, y, ref, value, left, right):
    text(x, y-9, left, 14)
    line(x, y, x+150, y)
    parts.append(f'<rect x="{x+150}" y="{y-8}" width="56" height="16" fill="white" stroke="#216351" stroke-width="2"/>')
    line(x+206, y, x+360, y)
    text(x+148, y-16, f'{ref} {value}', 14, True)
    text(x+222, y-9, right, 14)
    text(x+137, y+22, '1', 12)
    text(x+211, y+22, '2', 12)


def capacitor(x, y, ref, value, net):
    text(x, y-9, net, 14)
    line(x, y, x+167, y)
    line(x+167, y-11, x+167, y+11)
    line(x+180, y-11, x+180, y+11)
    line(x+180, y, x+360, y)
    text(x+149, y-18, f'{ref} {value}', 14, True)
    text(x+265, y-9, 'GND', 14)


text(30, 42, 'PLANTER / XIAO ESP32-C6 / REV A', 29, True)
text(30, 72, 'Electrical review: 2026-09-05. Matching net names connect across sections. All resistors/capacitors shown are FITTED.', 17)
box(30, 100, 495, 465, '1  Protected battery and complete divider')
text(50, 165, 'J1.1 P+ -- JP1.1 [removable link] JP1.2 -- PACK+', 16)
text(50, 196, 'J1.2 P- -------------------------------- GND', 16)
text(50, 228, 'PACK+ -> XIAO BAT+; GND -> XIAO BAT- and GND', 15)
resistor(65, 291, 'R2', '200k 1%', 'PACK+', 'VBAT_ADC')
resistor(65, 352, 'R3', '200k 1%', 'VBAT_ADC', 'GND')
capacitor(65, 416, 'C1', '100nF', 'VBAT_ADC')
text(50, 463, 'A0 = PACK+ / 2; at 4.2V pack, A0 = 2.1V.', 16, True)
text(50, 493, 'R3 is mandatory. XIAO R10 belongs to its charger.', 16)
text(50, 523, 'All battery loads, including R2, are after JP1.', 16)
text(50, 546, 'External common-port protection; no raw B- to GND.', 15)

box(545, 100, 510, 465, '2  Shared peripheral power / active LOW')
text(565, 165, 'Q1 AO3401A / SOT-23', 18, True)
text(565, 197, 'Pad 2 (S) = 3V3; pad 3 (D) = 3V3_SW', 17)
text(565, 228, 'Pad 1 (G) = GATE; body diode D -> S', 17)
resistor(580, 291, 'R1', '100k', '3V3', 'GATE')
resistor(580, 352, 'R4', '1k', 'PERIPH_EN_N', 'GATE')
capacitor(580, 416, 'C2', '10uF', '3V3_SW')
capacitor(580, 481, 'C3', '100nF', '3V3_SW')
text(565, 525, 'C2/C3 at J3. R1 holds Q1 off during reset.', 16)
text(565, 548, 'Park EPD outputs LOW before removing power.', 16)

box(1075, 100, 495, 465, '3  XIAO interface / U1')
xiao = [('1 D0 / GPIO0', 'VBAT_ADC'), ('2 D1 / GPIO1', 'MOIST_ADC'),
        ('3 D2 / GPIO2', 'PERIPH_EN_N'), ('4 D3 / GPIO21', 'EPD_CS'),
        ('5 D4 / GPIO22', 'EPD_DC'), ('6 D5 / GPIO23', 'EPD_RST'),
        ('7 D6 / GPIO16', 'EPD_BUSY'), ('8 D7 / GPIO17', 'SPARE_D7'),
        ('9 D8 / GPIO19', 'EPD_SCLK'), ('10 D9 / GPIO20', 'SPARE_D9'),
        ('11 D10 / GPIO18', 'EPD_MOSI'), ('12 3V3_OUT', '3V3'),
        ('13 GND', 'GND'), ('14 VBUS', 'NO CONNECT')]
for i, (pin, net) in enumerate(xiao):
    text(1095, 164+i*27, pin, 16)
    text(1330, 164+i*27, net, 16, True)

box(30, 585, 495, 415, '4  Moisture interface / J2')
text(50, 650, '1 = 3V3_SW     2 = MOIST_OUT     3 = GND', 17, True)
resistor(65, 719, 'R5', '1k', 'MOIST_OUT', 'MOIST_ADC')
capacitor(65, 790, 'C4', '100nF', 'MOIST_ADC')
capacitor(65, 861, 'C5', '100nF', '3V3_SW')
text(50, 910, 'C4 at U1; C5 at J2. Probe power is 3.3V.', 16)
text(50, 942, 'J2 connector/cable pin order must be confirmed.', 16)
text(50, 973, 'Cable ESD component selection remains open.', 16)

box(545, 585, 510, 415, '5  WeAct module / J3 2x4 / 2.54mm')
epd = [('1 BUSY', 'EPD_BUSY'), ('2 RES', 'EPD_RST'), ('3 D/C', 'EPD_DC'),
       ('4 CS', 'EPD_CS'), ('5 SCL', 'EPD_SCLK'), ('6 SDA', 'EPD_MOSI'),
       ('7 GND', 'GND'), ('8 VCC', '3V3_SW')]
for i, (pin, net) in enumerate(epd):
    text(570, 650+i*31, pin, 17)
    text(780, 650+i*31, net, 17, True)
text(570, 925, 'Header rows: [1 2] [3 4] [5 6] [7 8]', 16)
text(570, 953, 'MISO unused; BUSY uses an MCU pull-down.', 16)
text(570, 980, 'Verify actual panel controller before flashing.', 16)

box(1075, 585, 495, 415, '6  Carrier test points')
for i, net in enumerate(['PACK+', 'GND', '3V3', '3V3_SW', 'VBAT_ADC',
                          'MOIST_ADC', 'PERIPH_EN_N', 'EPD_BUSY', 'SPARE_D7', 'SPARE_D9']):
    text(1095, 650+i*33, f'TP{i+1}', 16)
    text(1210, 650+i*33, net, 16, True)

text(30, 1040, 'ASSEMBLY: underside BAT contacts and connector footprints need mechanical confirmation. USB charging cover deferred.', 17, True)
text(30, 1075, 'REVIEW STATUS: corrected circuit specification. Native KiCad capture/ERC, board layout/DRC and bench validation remain outstanding.', 17)
text(30, 1110, 'Sources: Seeed XIAO C6 schematic 2026-01-14 (03 Power); AOS AO3401A datasheet; supplied WeAct 1.54-inch board drawing.', 16)
text(30, 1138, 'Supporting files: netlist.csv, bom.csv, README.md, review.md. This SVG is not a fabrication release.', 16)
parts.append('</svg>')
Path(__file__).with_name('carrier-schematic.svg').write_text('\n'.join(parts)+'\n')
