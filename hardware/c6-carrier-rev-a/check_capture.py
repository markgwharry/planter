"""Check terminal coverage and circuit invariants in the capture checklist.

This guards against conflicting net assignments and omitted terminals. It is
not a substitute for KiCad ERC, footprint verification or physical testing.
"""
import csv
from pathlib import Path

ROOT = Path(__file__).parent


def check(rows, bom):
    pins = {}
    for row in rows:
        assert None not in row and all(row[k] for k in ('Net', 'From', 'To')), row
        for pin in (row['From'], row['To']):
            if pin in ('GND', 'NC'):
                continue
            assert pin not in pins or pins[pin] == row['Net'], f'{pin} assigned to two nets'
            pins[pin] = row['Net']
    expected = {f'R{i}.{p}' for i in range(1, 6) for p in (1, 2)}
    expected |= {f'C{i}.{p}' for i in range(1, 6) for p in (1, 2)}
    expected |= {f'J{j}.{p}' for j, n in ((1, 2), (2, 3), (3, 8)) for p in range(1, n+1)}
    expected |= {'Q1.G', 'Q1.S', 'Q1.D', 'JP1.1', 'JP1.2'}
    expected |= {f'TP{i}' for i in range(1, 11)}
    expected |= {'U1.BAT+', 'U1.BAT-', 'U1.GND', 'U1.3V3', 'U1.VBUS', 'U1.D0-A0', 'U1.D1-A1'}
    expected |= {f'U1.D{i}' for i in range(2, 11)}
    assert set(pins) == expected, f'Missing: {expected-set(pins)}; unexpected: {set(pins)-expected}'
    for row in bom:
        ref = row['Reference']
        assert row['Fit'] == 'Fitted', f'Unresolved population option: {ref}'
        if ref == 'TP1-TP10':
            continue
        assert any(p.startswith(ref+'.') for p in pins), f'BOM part absent from capture: {ref}'
    required = {'R2.1': 'PACK+', 'R2.2': 'VBAT_ADC', 'R3.1': 'VBAT_ADC',
                'R3.2': 'GND', 'R1.1': '3V3', 'R1.2': 'GATE',
                'Q1.G': 'GATE', 'Q1.S': '3V3', 'Q1.D': '3V3_SW',
                'JP1.1': 'PACK_IN+', 'JP1.2': 'PACK+', 'U1.BAT+': 'PACK+',
                'U1.BAT-': 'GND', 'U1.D0-A0': 'VBAT_ADC', 'U1.D1-A1': 'MOIST_ADC',
                'J3.7': 'GND', 'J3.8': '3V3_SW', 'U1.VBUS': 'NC_VBUS'}
    for pin, net in required.items():
        assert pins[pin] == net, f'{pin}: expected {net}, got {pins[pin]}'
    return len(pins)


if __name__ == '__main__':
    with (ROOT/'netlist.csv').open() as f:
        rows = list(csv.DictReader(f))
    with (ROOT/'bom.csv').open() as f:
        bom = list(csv.DictReader(f))
    count = check(rows, bom)
    # Demonstrate that the two actual review defects fail the checker.
    bad_gate = [dict(r) for r in rows]
    next(r for r in bad_gate if r['From'] == 'R1.2')['Net'] = '3V3'
    missing_bottom = [r for r in rows if 'R3.' not in r['From'] and 'R3.' not in r['To']]
    for bad in (bad_gate, missing_bottom):
        try:
            check(bad, bom)
        except AssertionError:
            pass
        else:
            raise AssertionError('Review defect was not detected')
    print(f'PASS: {count} terminals checked; missing divider and conflicting gate regressions rejected.')
