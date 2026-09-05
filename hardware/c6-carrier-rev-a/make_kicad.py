"""Rebuild the Rev A schematic. Requires the standard KiCad 10 symbol library.

The checked-in schematic is editable normally in KiCad; this generator is a
reproducible capture baseline, not a round-trip editor. Do not rerun over GUI
edits without committing them first. No production PCB footprint is invented.
"""
import csv
import json
import math
import os
from pathlib import Path
import re
import uuid

ROOT = Path(__file__).parent
LIB = Path(os.environ.get('KICAD10_SYMBOL_DIR',
    '/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols'))
PROJECT = 'planter-c6-rev-a'
NS = uuid.UUID('a1945917-f02e-4c10-bcd6-4aad603c7f8a')


def uid(key):
    return str(uuid.uuid5(NS, key))


def quote(s):
    return json.dumps(str(s), ensure_ascii=False)


def parse(raw):
    stack = [[]]
    for token in re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', raw):
        if token == '(':
            item = []
            stack[-1].append(item)
            stack.append(item)
        elif token == ')':
            stack.pop()
        else:
            stack[-1].append(json.loads(token) if token.startswith('"') else token)
    assert len(stack) == 1
    return stack[0][0]


def child(tree, key):
    return next(x for x in tree if isinstance(x, list) and x[0] == key)


def symbol_source(lib, name):
    raw = (LIB / f'{lib}.kicad_sym').read_text()
    start = raw.index(f'(symbol "{name}"\n')
    depth = 0
    for match in re.finditer(r'"(?:\\.|[^"\\])*"|[()]', raw[start:]):
        if match.group() == '(':
            depth += 1
        elif match.group() == ')':
            depth -= 1
            if not depth:
                return raw[start:start + match.end()]
    raise ValueError(name)


def effects(size=1.27, justify='', hide=False):
    return f'(effects (font (size {size} {size}))' + (
        f' (justify {justify})' if justify else '') + (' (hide yes)' if hide else '') + ')'


def prop(name, value, x, y, hide=False, justify='', angle=0):
    return f'(property {quote(name)} {quote(value)} (at {x} {y} {angle}) {effects(justify=justify, hide=hide)})'


def xiao_symbol():
    # Edge contact numbers follow Seeed's module connector in the official
    # 2026-01-14 schematic. BAT+/- are separate underside solder contacts, NOT
    # invented castellated pad 15/16; physical footprint assignment is pending.
    left = [('1', 'D0/A0_GPIO0', 'input'), ('2', 'D1/A1_GPIO1', 'input'),
            ('3', 'D2_GPIO2', 'output'), ('4', 'D3_GPIO21', 'output'),
            ('5', 'D4_GPIO22', 'output'), ('6', 'D5_GPIO23', 'output'),
            ('7', 'D6_GPIO16', 'input'), ('8', 'D7_GPIO17', 'bidirectional')]
    right = [('14', 'VBUS', 'power_in'), ('13', 'GND', 'power_in'),
             ('12', '3V3_OUT', 'power_out'), ('BAT+', 'BAT+_underside', 'passive'),
             ('BAT-', 'BAT-_underside', 'power_in'), ('9', 'D8_GPIO19', 'output'),
             ('10', 'D9_GPIO20', 'bidirectional'), ('11', 'D10_GPIO18', 'output')]
    parts = ['(symbol "XIAO_ESP32C6_Carrier" (pin_names (offset 1.27)) '
             '(in_bom yes) (on_board yes)', prop('Reference', 'U', 0, 34.29),
             prop('Value', 'XIAO_ESP32C6_Carrier', 0, 31.75),
             '(symbol "XIAO_ESP32C6_Carrier_0_1" (rectangle '
             '(start -30.48 30.48) (end 30.48 -30.48) '
             '(stroke (width 0.254) (type default)) (fill (type background))))',
             '(symbol "XIAO_ESP32C6_Carrier_1_1"']
    for side, pins in ((-1, left), (1, right)):
        for i, (number, name, kind) in enumerate(pins):
            x, y, angle = side * 35.56, 26.67 - i * 7.62, 0 if side < 0 else 180
            parts.append(f'(pin {kind} line (at {x} {y:.2f} {angle}) (length 5.08) '
                         f'(name {quote(name)} {effects()}) (number {quote(number)} {effects()}))')
    return '\n'.join(parts) + '))'


SOURCES = {name: symbol_source(lib, name) for lib, name in [
    ('Device', 'R'), ('Device', 'C'), ('Transistor_FET', 'Q_PMOS_GSD'),
    ('Connector_Generic', 'Conn_01x02'), ('Connector_Generic', 'Conn_01x03'),
    ('Connector_Generic', 'Conn_02x04_Odd_Even'), ('Connector', 'TestPoint'),
    ('power', 'PWR_FLAG')]}
SOURCES['XIAO_ESP32C6_Carrier'] = xiao_symbol()
ITEMS, POS = [], {}


def grid(n):
    return round(round(n / 1.27) * 1.27, 2)


def add(ref, kind, value, x, y, angle=0, footprint='', field=None):
    x, y = grid(x), grid(y)
    raw = SOURCES[kind]
    tree = parse(raw)
    pins = []
    for sub in tree:
        if isinstance(sub, list) and sub[0] == 'symbol':
            for pin in sub:
                if isinstance(pin, list) and pin[0] == 'pin':
                    number = child(pin, 'number')[1]
                    px, py, pa = map(float, child(pin, 'at')[1:])
                    theta = math.radians(angle)
                    xx = round(x + px * math.cos(theta) - py * math.sin(theta), 2)
                    yy = round(y - px * math.sin(theta) - py * math.cos(theta), 2)
                    POS[f'{ref}.{number}'] = (xx, yy, (pa + angle) % 360)
                    pins.append(f'(pin {quote(number)} (uuid "{uid(ref+number)}"))')
    fx, fy = field or (x + 4, y - 2.54)
    if ref.startswith(('J', 'U')):
        fx, fy = field or (x, y - (36.83 if ref.startswith('U') else 7.62))
    ITEMS.append(f'(symbol (lib_id "Planter:{kind}") (at {x} {y} {angle}) (unit 1) '
                 f'(in_bom {"no" if ref.startswith("#") else "yes"}) (on_board yes) '
                 f'(dnp no) (uuid "{uid(ref)}")\n' +
                 prop('Reference', ref, fx, fy, hide=ref.startswith('#'), angle=angle % 180) + '\n' +
                 prop('Value', value, fx, fy + 2.54, hide=ref.startswith(('#','TP')), angle=angle % 180) + '\n' +
                 prop('Footprint', footprint, x, y, hide=True) + '\n' +
                 '\n'.join(pins) + f'\n(instances (project "{PROJECT}" '
                 f'(path "/{uid("root")}" (reference {quote(ref)}) (unit 1)))))')


def wire(a, b):
    a, b = a[:2], b[:2]
    assert a != b
    ITEMS.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) '
                 f'(stroke (width 0) (type default)) (uuid "{uid(str((a,b)))}"))')


def label(net, point, right=False):
    x, y = point[:2]
    ITEMS.append(f'(label {quote(net)} (at {x} {y} 0) '
                 f'{effects(justify="right bottom" if right else "left bottom")} '
                 f'(uuid "{uid(str((net,x,y)))}"))')


def stub(pin, net, length=5.08):
    x, y, angle = POS[pin]
    # Pin orientation points into the body; extend outwards.
    dx, dy = -math.cos(math.radians(angle)), math.sin(math.radians(angle))
    end = (round(x + length * dx, 2), round(y + length * dy, 2))
    wire((x, y), end)
    label(net, end, right=dx < -0.5)


def text(s, x, y, size=1.5):
    ITEMS.append(f'(text {quote(s)} (at {x} {y} 0) {effects(size, "left top")} '
                 f'(uuid "{uid(s)}"))')


def main():
    resistor = 'Resistor_SMD:R_0603_1608Metric'
    capacitor = 'Capacitor_SMD:C_0603_1608Metric'
    add('J1', 'Conn_01x02', 'PROTECTED BATTERY', 50.8, 50.8)
    add('JP1', 'Conn_01x02', 'CURRENT LINK', 121.92, 50.8,
        footprint='Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical')
    add('#FLG01', 'PWR_FLAG', 'PWR_FLAG', 173.99, 50.8)
    add('#FLG02', 'PWR_FLAG', 'PWR_FLAG', 173.99, 69.85)
    add('U1', 'XIAO_ESP32C6_Carrier', 'Seeed XIAO ESP32-C6', 304.8, 76.2)
    add('R2', 'R', '200k 1%', 45.72, 109.22, footprint=resistor, field=(57.15,106.68))
    add('R3', 'R', '200k 1%', 45.72, 134.62, footprint=resistor, field=(57.15,132.08))
    add('C1', 'C', '100nF', 86.36, 134.62, footprint=capacitor, field=(96.52,132.08))
    add('Q1', 'Q_PMOS_GSD', 'AO3401A', 137.16, 182.88,
        footprint='Package_TO_SOT_SMD:SOT-23', field=(153.67,181.61))
    add('R1', 'R', '100k 1%', 104.14, 160.02, footprint=resistor, field=(115.57,157.48))
    add('R4', 'R', '1k', 76.2, 182.88, 90, resistor, field=(76.2,174))
    add('J2', 'Conn_01x03', 'MOISTURE PROBE', 238.76, 140.97)
    add('R5', 'R', '1k', 274.32, 160.02, 90, resistor, field=(274.32,151.13))
    add('C4', 'C', '100nF', 302.26, 175.26, footprint=capacitor, field=(313.69,172.72))
    add('C5', 'C', '100nF', 238.76, 182.88, footprint=capacitor, field=(250.19,180.34))
    add('J3', 'Conn_02x04_Odd_Even', 'WEACT 1.54 INCH', 358.14, 147.32)
    add('C2', 'C', '10uF', 345.44, 187.96,
        footprint='Capacitor_SMD:C_0805_2012Metric', field=(353.06,185.42))
    add('C3', 'C', '100nF', 383.54, 187.96, footprint=capacitor, field=(392.43,185.42))
    for i in range(1, 11):
        x = 38.1 + (i-1)*38.1
        add(f'TP{i}', 'TestPoint', f'TP{i}', x, 226.06,
            footprint='TestPoint:TestPoint_Pad_D1.5mm', field=(x,216.535))
    # The checked CSV remains an independent logical pin naming scheme.
    mapping = {'BAT+':'BAT+', 'BAT-':'BAT-', 'GND':'13', '3V3':'12', 'VBUS':'14',
               'D0-A0':'1', 'D1-A1':'2', **{f'D{i}':str(i+1) for i in range(2,11)}}
    def physical(logical):
        if logical.startswith('U1.'):
            return 'U1.' + mapping[logical[3:]]
        if logical.startswith('Q1.'):
            return 'Q1.' + {'G':'1', 'S':'2', 'D':'3'}[logical[3:]]
        if logical.startswith('TP'):
            return logical + '.1'
        return logical
    nets = {}
    with (ROOT/'netlist.csv').open() as f:
        for row in csv.DictReader(f):
            for endpoint in (row['From'], row['To']):
                if endpoint not in ('GND', 'NC'):
                    pin = physical(endpoint)
                    assert pin not in nets or nets[pin] == row['Net']
                    nets[pin] = row['Net']
    # Connect the two analogue filters and the MOSFET gate directly on paper.
    routes = [(['R2.2','R3.1','C1.1'], [(45.72,121.92),(45.72,121.92),(86.36,121.92)]),
              (['R1.2','R4.2','Q1.1'], [(104.14,182.88)]*3),
              (['R5.2','C4.1'], [(302.26,160.02)]*2)]
    handled = set()
    for pins, mids in routes:
        net = nets[pins[0]]
        for pin, mid in zip(pins,mids):
            assert nets[pin] == net
            wire(POS[pin], mid)
            handled.add(pin)
        unique = list(dict.fromkeys(mids))
        for a,b in zip(unique,unique[1:]):
            wire(a,b)
        for mid in set(mids):
            if mids.count(mid) > 1 or len(unique) > 1:
                # A junction is only needed where at least three segments meet.
                if mids.count(mid) >= 2 and (len(pins) > 2):
                    ITEMS.append(f'(junction (at {mid[0]} {mid[1]}) (diameter 0) '
                                 f'(color 0 0 0 0) (uuid "{uid(str(mid))}"))')
        label(net, unique[-1])
    for pin, net in nets.items():
        if pin in handled:
            continue
        if net == 'NC_VBUS':
            x,y,_ = POS[pin]
            ITEMS.append(f'(no_connect (at {x} {y}) (uuid "{uid("NC_VBUS")}"))')
        else:
            stub(pin, net)
    stub('#FLG01.1', 'PACK+')
    stub('#FLG02.1', 'GND')
    text('PLANTER / C6 CARRIER - REV A', 20, 15, 3)
    text('01  PROTECTED BATTERY INPUT', 20, 30, 2)
    text('JP1: fitted removable shunt. All battery loads are downstream.\n'
         'PWR_FLAG declares the external pack supply; it is not a component.',20,65)
    text('External 1S protection PCB stays with the cell.\n'
         'Cell + / - to B+ / B-. P+ to J1.1; P- to J1.2.\n'
         'Do not join B- to system GND. Confirm common-port charging.',20,78)
    text('02  BATTERY MEASUREMENT',20,94,2)
    text('BOTH 200k resistors fitted. 4.2V pack -> 2.1V ADC.\n'
         'C1 at U1. XIAO has no onboard A0 divider.',20,146)
    text('03  SHARED PERIPHERAL POWER',20,163,2)
    text('LOW = on. R1 holds Q1 off at reset. Q1: G=1, S=2, D=3.\n'
         'Source is the lower pin here (3V3); drain is 3V3_SW.\n'
         'Firmware must not drive an unpowered peripheral high.',20,197)
    text('04  XIAO MODULE',213,30,2)
    text('VBUS intentionally NC. USB-C on XIAO is the only charger.\n'
         'BAT+/-: short strain-relieved underside wires for prototype.\n'
         'Module footprint and antenna keepout must be verified before layout.',213,112)
    text('05  PROBE / FILTER',213,127,2)
    text('06  DISPLAY / LOCAL BYPASS',327,127,2)
    text('J2 keyed part TBD. C4 at U1; C5 at J2.\n'
         'J3 odd/even pinout from WeAct drawing; cable orientation TBD.',213,202)
    text('07  TEST POINTS',20,212,2)
    text('PROTOTYPE CAPTURE - NOT A FABRICATION RELEASE',20,243,2)
    text('No on-carrier lithium protection or duplicate charger. Identify and insulate the external protection board.\n'
         'J1/J2/J3 and U1 footprints are intentionally unassigned pending exact parts and mechanical checks.\n'
         'Confirm actual display controller, cable ESD requirements, enclosure clearances and charging access.\n'
         'Before field use: polarity/continuity, ADC calibration, rail-off leakage, USB charging and 24-hour bench soak.',20,251)
    library = '(kicad_symbol_lib (version 20250114) (generator "planter_capture")\n' + '\n'.join(SOURCES.values()) + ')\n'
    (ROOT/'Planter.kicad_sym').write_text(library)
    embedded = '\n'.join(raw.replace(f'(symbol "{name}"',f'(symbol "Planter:{name}"',1)
                         for name,raw in SOURCES.items())
    schematic = f'(kicad_sch (version 20250114) (generator "planter_capture") '
    schematic += f'(uuid "{uid("root")}") (paper "A3")\n'
    schematic += '(title_block (title "Planter XIAO ESP32-C6 carrier") (date "2026-09-05") '
    schematic += '(rev "A") (comment 1 "External protected 1S pack; not fabrication-ready"))\n'
    schematic += '(lib_symbols\n' + embedded + ')\n' + '\n'.join(ITEMS)
    schematic += '\n(sheet_instances (path "/" (page "1"))))\n'
    (ROOT/f'{PROJECT}.kicad_sch').write_text(schematic)
    (ROOT/'sym-lib-table').write_text('(sym_lib_table (version 7)\n'
        '  (lib (name "Planter") (type "KiCad") (uri "${KIPRJMOD}/Planter.kicad_sym") '
        '(options "") (descr "Self-contained Rev A carrier capture"))\n)\n')
    print(f'Wrote {PROJECT}.kicad_sch: {len(nets)} terminal assignments.')


if __name__ == '__main__':
    main()
