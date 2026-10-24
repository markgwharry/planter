"""Export KiCad's actual connectivity and compare it with the review checklist.

Run with Python 3; uses kicad-cli on PATH or the standard macOS installation.
KICAD_CLI may override the executable. Also checks values and rejects four
deliberately introduced electrical errors. Does not regenerate the schematic.
"""
import copy
import csv
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from check_capture import check

ROOT = Path(__file__).parent
CLI = os.environ.get('KICAD_CLI') or shutil.which('kicad-cli') or (
    '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli')
# Independent, explicit physical-to-logical mapping, reviewed against Seeed.
XIAO = {'1':'D0-A0', '2':'D1-A1', '3':'D2', '4':'D3', '5':'D4', '6':'D5',
        '7':'D6', '8':'D7', '9':'D8', '10':'D9', '11':'D10', '12':'3V3',
        '13':'GND', '14':'VBUS', 'BAT+':'BAT+', 'BAT-':'BAT-'}


def logical(ref, pin):
    if ref == 'U1':
        return 'U1.' + XIAO[pin]
    if ref == 'Q1':
        return 'Q1.' + {'1':'G', '2':'S', '3':'D'}[pin]
    if ref.startswith('TP'):
        assert pin == '1'
        return ref
    return f'{ref}.{pin}'


def validate(xml, rows, bom):
    check(rows, bom)
    expected = {}
    for row in rows:
        for pin in (row['From'], row['To']):
            if pin not in ('GND','NC'):
                expected[pin] = row['Net']
    actual = {}
    for net in xml.findall('./nets/net'):
        for node in net.findall('node'):
            ref, pin = node.attrib['ref'], node.attrib['pin']
            assert not ref.startswith('#'), 'Unexpected power flag in board netlist'
            name = net.attrib['name'].removeprefix('/')
            if ref == 'U1' and pin == '14':
                assert node.attrib['pintype'].endswith('+no_connect'), 'VBUS must be explicitly NC'
                assert len(net.findall('node')) == 1, 'VBUS connected to another terminal'
                name = 'NC_VBUS'
            endpoint = logical(ref,pin)
            assert endpoint not in actual, f'Duplicate endpoint: {endpoint}'
            actual[endpoint] = name
    assert actual == expected, f'Netlist mismatch: {set(actual.items()) ^ set(expected.items())}'
    expected_values = {'R1':'100k 1%', 'R2':'200k 1%', 'R3':'200k 1%',
                       'R4':'1k', 'R5':'1k', 'C1':'100nF', 'C2':'10uF',
                       'C3':'100nF', 'C4':'100nF', 'C5':'100nF', 'Q1':'AO3401A'}
    components = {x.attrib['ref']:x for x in xml.findall('./components/comp')}
    assert set(components) == {p.split('.')[0] for p in expected}
    for ref, value in expected_values.items():
        assert components[ref].findtext('value') == value, f'{ref} value is wrong'
    assert components['Q1'].findtext('footprint') == 'Package_TO_SOT_SMD:SOT-23'
    for ref in ('U1', 'J1', 'J2', 'J3'):
        assert not components[ref].findtext('footprint'), f'{ref} footprint is not yet verified'
    return len(actual), len(components)


def main():
    with (ROOT/'netlist.csv').open() as f:
        rows = list(csv.DictReader(f))
    with (ROOT/'bom.csv').open() as f:
        bom = list(csv.DictReader(f))
    with tempfile.TemporaryDirectory(prefix='planter-netlist-') as tmp:
        output = Path(tmp)/'netlist.xml'
        subprocess.run([CLI, 'sch', 'export', 'netlist', str(ROOT/'planter-c6-rev-a.kicad_sch'),
                        '--format', 'kicadxml', '-o', str(output)], check=True)
        xml = ET.parse(output).getroot()
    count, components = validate(xml,rows,bom)
    bad_cases = [copy.deepcopy(xml) for _ in range(4)]
    bad_cases[0].find('./nets/net/node[@ref="Q1"][@pin="2"]').set('pin','3')
    divider = bad_cases[1].find('./nets/net/node[@ref="R3"][@pin="2"]')
    divider.set('pin','1')
    header = bad_cases[2].find('./nets/net/node[@ref="J3"][@pin="8"]')
    header.set('pin','7')
    bad_cases[3].find('./components/comp[@ref="R2"]/value').text = '10k'
    for bad in bad_cases:
        try:
            validate(bad,rows,bom)
        except (AssertionError, KeyError):
            pass
        else:
            raise AssertionError('Injected schematic defect escaped validation')
    print(f'PASS: {count} KiCad terminals, {components} components, values, VBUS NC and pending footprints verified.')
    print('PASS: swapped MOSFET pins, missing divider terminal, swapped display power pins and wrong divider value rejected.')


if __name__ == '__main__':
    main()
