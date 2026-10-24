"""Build the Rev B board from the Rev B schematic. Run with KiCad's own Python:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 make_pcb.py

Nets, references, values, footprints and symbol links all come from the
schematic's exported netlist, so DRC schematic parity holds by construction.
Placement and routing are explicit below: board coordinates in mm from the top
left corner, top view, USB-C at the bottom edge.

Like make_kicad.py this is a reproducible baseline, not a round-trip editor.
Commit any GUI edits to the .kicad_pcb before rerunning.
"""
import math
import re
import subprocess
from pathlib import Path

import pcbnew

ROOT = Path(__file__).parent
PROJECT = 'planter-carrier-rev-b'
KICAD = Path('/Applications/KiCad/KiCad.app/Contents')
FP_LIBS = KICAD / 'SharedSupport/footprints'
ORIGIN = (100.0, 100.0)          # sheet position of the board's top-left corner
W, H = 50.12, 32.60              # WeAct 1.54-inch module outline
HOLES = [(2.80, 2.80), (47.32, 2.80), (2.80, 29.80), (47.32, 29.80)]  # 44.52 x 27.00
NOTCH = (46.97, 6.30, 8.80, 23.80, 26.30)   # FPC fold recess on the right edge
CORNER_R = 2.5
XIAO = (35.10, 20.68)            # centre of the 2x7 pin field
ANTENNA_KEEPOUT = (24.5, 0.0, W, 11.5)       # no copper at all
ANTENNA_NO_POUR = (25.67, 11.5, 44.53, 16.5)  # tracks allowed, no plane

TRACK, POWER = 0.25, 0.5
VIA_D, VIA_DRILL = 0.6, 0.3


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y))


# ── Custom footprint ─────────────────────────────────────────────────────────
def write_xiao_footprint():
    """Socketed XIAO: two 1x7 2.54mm rows 15.24mm apart, USB toward +y.

    Pad numbers match the castellations: 1-7 = D0-D6, 8-11 = D7-D10, 12 3V3,
    13 GND, 14 5V. Coordinates derived from Seeed's XIAO ESP32-S3 Eagle
    package (castellation centres 1.27mm inside a 17.78mm body); the C6 shares
    the outline. Courtyard covers only the two header strips: the module sits
    on the sockets, so 0603 parts may be placed under it.
    """
    pads = [(n, 7.62, 7.62 - (n - 1) * 2.54) for n in range(1, 8)]
    pads += [(n, -7.62, -7.62 + (n - 8) * 2.54) for n in range(8, 15)]
    lines = ['(footprint "Seeed_XIAO_Socketed_2x7_P2.54mm" (version 20241229) (generator "planter_capture")',
             '  (layer "F.Cu")',
             '  (descr "Seeed XIAO (ESP32-C6 / ESP32-S3) on 2.54mm sockets or pins, USB-C toward +y")',
             '  (attr through_hole)',
             '  (property "Reference" "U**" (at 0 -1.5 0) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))',
             '  (property "Value" "XIAO" (at 0 1.5 0) (layer "F.Fab") (effects (font (size 0.8 0.8) (thickness 0.12))))']
    def line(layer, a, b, w=0.12):
        lines.append(f'  (fp_line (start {a[0]} {a[1]}) (end {b[0]} {b[1]}) (stroke (width {w}) (type solid)) (layer "{layer}"))')
    def rect(layer, x0, y0, x1, y1, w=0.12):
        for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            line(layer, a, b, w)
    rect('F.Fab', -8.89, -10.68, 8.89, 10.455, 0.1)
    rect('F.Fab', -4.5, 4.57, 4.5, 11.92, 0.1)            # USB-C receptacle
    rect('F.SilkS', -8.95, -10.74, 8.95, 10.515)
    # 0.25mm courtyard margin (not KiCad's 0.5) so H4's courtyard clears it;
    # the M2.5 screw head itself clears the socket by ~1mm.
    rect('F.CrtYd', -9.14, -9.14, -6.10, 9.14, 0.05)
    rect('F.CrtYd', 6.10, -9.14, 9.14, 9.14, 0.05)
    lines.append('  (fp_circle (center 9.6 7.62) (end 9.85 7.62) (stroke (width 0.12) (type solid)) (fill solid) (layer "F.SilkS"))')
    for text, y in (('ANT', -8.9), ('USB', 9.3)):
        lines.append(f'  (fp_text user "{text}" (at 0 {y} 0) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))')
    for n, x, y in pads:
        shape = 'rect' if n == 1 else 'circle'
        lines.append(f'  (pad "{n}" thru_hole {shape} (at {x} {y}) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))')
    lines.append(')')
    lib = ROOT / 'Planter.pretty'
    lib.mkdir(exist_ok=True)
    (lib / 'Seeed_XIAO_Socketed_2x7_P2.54mm.kicad_mod').write_text('\n'.join(lines) + '\n')
    (ROOT / 'fp-lib-table').write_text('(fp_lib_table (version 7)\n'
        '  (lib (name "Planter") (type "KiCad") (uri "${KIPRJMOD}/Planter.pretty") '
        '(options "") (descr "Rev B carrier footprints"))\n)\n')


# ── Netlist from the schematic ───────────────────────────────────────────────
def parse(raw):
    stack = [[]]
    for token in re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', raw):
        if token == '(':
            stack[-1].append([]); stack.append(stack[-1][-1])
        elif token == ')':
            stack.pop()
        else:
            stack[-1].append(token[1:-1].replace('\\"', '"') if token.startswith('"') else token)
    return stack[0][0]


def kids(tree, key):
    return [x for x in tree if isinstance(x, list) and x and x[0] == key]


def kid(tree, key):
    return kids(tree, key)[0]


def read_netlist():
    out = ROOT / 'build' / f'{PROJECT}.net'
    out.parent.mkdir(exist_ok=True)
    subprocess.run([str(KICAD / 'MacOS/kicad-cli'), 'sch', 'export', 'netlist',
                    '--format', 'kicadsexpr', '-o', str(out),
                    str(ROOT / f'{PROJECT}.kicad_sch')], check=True, capture_output=True)
    tree = parse(out.read_text())
    comps = {}
    for comp in kids(kid(tree, 'components'), 'comp'):
        ref = kid(comp, 'ref')[1]
        if ref.startswith('#'):
            continue
        comps[ref] = dict(value=kid(comp, 'value')[1], footprint=kid(comp, 'footprint')[1],
                          path=kid(kid(comp, 'sheetpath'), 'tstamps')[1] + kid(comp, 'tstamps')[1])
    pins = {}
    for net in kids(kid(tree, 'nets'), 'net'):
        name = kid(net, 'name')[1]
        for node in kids(net, 'node'):
            pins[(kid(node, 'ref')[1], kid(node, 'pin')[1])] = name
    return comps, pins


# ── Placement: ref -> (x, y, rotation, bottom side) ──────────────────────────
PLACE = {
    'U1': (*XIAO, 0, False),
    'J3': (1.93, 12.49, 0, True),     # pin 1; flipped so pin 2 lands at +x
    'J1': (17.40, 25.70, 0, False),
    'J2': (8.30, 25.70, 0, False),
    'JP1': (17.40, 21.90, 180, False),
    'J4': (23.80, 21.80, 0, False),
    'TP1': (20.60, 18.90, 0, False),
    'TP2': (12.00, 6.00, 0, False),
    'TP3': (12.00, 20.00, 0, False),
    'C2': (8.00, 20.90, 0, False),
    'C3': (8.00, 22.75, 0, False),
    'C5': (12.00, 22.75, 0, False),
    'Q1': (31.40, 23.22, 0, False),
    'R1': (30.46, 19.90, 270, False),
    'R4': (37.50, 22.27, 180, False),
    'R5': (37.00, 25.76, 0, False),
    'C4': (40.00, 26.585, 270, False),
    'R2': (35.00, 28.30, 0, False),
    'R3': (38.00, 29.70, 270, False),
    'C1': (40.00, 29.70, 270, False),
}


def load_fp(fpid):
    lib, name = fpid.split(':')
    path = ROOT / 'Planter.pretty' if lib == 'Planter' else FP_LIBS / f'{lib}.pretty'
    fp = pcbnew.FootprintLoad(str(path), name)
    assert fp, fpid
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def build():
    write_xiao_footprint()
    comps, pins = read_netlist()
    board = pcbnew.CreateEmptyBoard()
    board.SetCopperLayerCount(2)
    nets = {}
    for name in sorted(set(pins.values())):
        nets[name] = pcbnew.NETINFO_ITEM(board, name)
        board.Add(nets[name])
    fps = {}
    for ref, c in comps.items():
        fp = load_fp(c['footprint'])
        fp.SetReference(ref)
        fp.SetValue(c['value'])
        fp.SetPath(pcbnew.KIID_PATH(c['path']))
        fp.SetSheetname('Root')
        fp.SetSheetfile(f'{PROJECT}.kicad_sch')
        if ref != 'U1':
            # Functional labels go on silk (see silk()); designators on Fab
            # keep the dense areas legible and off the pads.
            fp.Reference().SetLayer(pcbnew.F_Fab)
        x, y, rot, bottom = PLACE[ref]
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        board.Add(fp)
        if bottom:
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        for pad in fp.Pads():
            name = pins.get((ref, pad.GetNumber()))
            if name:
                pad.SetNet(nets[name])
        fps[ref] = fp
    # U1.13 sits in a narrow top-plane sliver between PACK+ and 3V3; connect it
    # solid rather than by spokes that can only reach that sliver.
    next(x for x in fps['U1'].Pads() if x.GetNumber() == '13').SetLocalZoneConnection(
        pcbnew.ZONE_CONNECTION_FULL)
    for i, (x, y) in enumerate(HOLES, 1):
        fp = load_fp('MountingHole:MountingHole_2.7mm_M2.5')
        fp.SetReference(f'H{i}')
        fp.SetValue('M2.5')
        fp.SetPosition(pt(x, y))
        fp.SetBoardOnly(True)
        fp.Reference().SetLayer(pcbnew.F_Fab)
        fp.SetExcludedFromBOM(True)
        fp.SetExcludedFromPosFiles(True)
        board.Add(fp)
    outline(board)
    route(board, fps, nets)
    zones(board, nets)
    silk(board)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    path = ROOT / f'{PROJECT}.kicad_pcb'
    pcbnew.SaveBoard(str(path), board)
    print(f'Wrote {path.name}: {len(fps)} parts + {len(HOLES)} holes, '
          f'{len(board.GetTracks())} tracks/vias, {len(nets)} nets.')


# ── Board outline ────────────────────────────────────────────────────────────
def edge(board, a, b):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pt(*a)); s.SetEnd(pt(*b))
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    board.Add(s)


def corner(board, cx, cy, start_deg):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetCenter(pt(cx, cy))
    a = math.radians(start_deg)
    s.SetStart(pt(cx + CORNER_R * math.cos(a), cy + CORNER_R * math.sin(a)))
    s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T), True)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    board.Add(s)


def outline(board):
    r = CORNER_R
    nx, ny0, ny1, ny2, ny3 = NOTCH
    edge(board, (r, 0), (W - r, 0))
    edge(board, (W, r), (W, ny0))
    edge(board, (W, ny0), (nx, ny1))
    edge(board, (nx, ny1), (nx, ny2))
    edge(board, (nx, ny2), (W, ny3))
    edge(board, (W, ny3), (W, H - r))
    edge(board, (W - r, H), (r, H))
    edge(board, (0, H - r), (0, r))
    corner(board, r, r, 180)
    corner(board, W - r, r, 270)
    corner(board, W - r, H - r, 0)
    corner(board, r, H - r, 90)


# ── Routing ──────────────────────────────────────────────────────────────────
F, B = pcbnew.F_Cu, pcbnew.B_Cu

# Each route: (net, layer, width, [points]). A point is (x, y) or 'REF.PAD'.
ROUTES = [
    # Display bus. BUSY/SCLK/MOSI run on top from J3 to the XIAO's left row;
    # RST/DC/CS cross under the module on the bottom to its right row, each
    # threading one gap between left-row pads.
    ('EPD_BUSY', F, TRACK, ['J3.1', (1.93, 11.22), (24.00, 11.22), (25.84, 13.06), 'U1.8']),
    ('EPD_SCLK', F, TRACK, ['J3.5', (1.93, 16.30), (25.60, 16.30), (26.30, 15.60), 'U1.9']),
    ('EPD_MOSI', F, TRACK, ['J3.6', (25.50, 17.57), (25.50, 19.50), (26.68, 20.68), 'U1.11']),
    ('EPD_RST', B, TRACK, ['J3.2', (24.50, 12.49), (26.34, 14.33), (41.20, 14.33), 'U1.6']),
    ('EPD_DC', B, TRACK, ['J3.3', (1.93, 13.76), (23.00, 13.76), (26.11, 16.87), (41.20, 16.87), 'U1.5']),
    ('EPD_CS', B, TRACK, ['J3.4', (21.50, 15.03), (25.88, 19.41), (41.20, 19.41), 'U1.4']),
    # Battery: J1 -> JP1 current link -> PACK+ -> J4 leads to the XIAO, and the divider.
    ('PACK_IN+', F, POWER, ['J1.1', 'JP1.1']),
    ('PACK+', F, POWER, ['JP1.2', (17.86, 18.90), 'TP1.1', (21.20, 18.90), (23.80, 21.50), 'J4.1']),
    ('PACK+', F, 0.3, ['J4.1', (25.30, 21.80), (25.30, 26.40), (25.93, 27.03), (33.20, 27.03), 'R2.1']),
    ('VBAT_ADC', F, TRACK, ['R2.2', 'R3.1', 'C1.1', 'U1.1']),
    # Peripheral switch, under the module.
    ('3V3', F, 0.3, ['U1.12', (28.90, 23.22), (28.90, 24.17), 'Q1.2']),
    ('3V3', F, TRACK, [(28.90, 23.22), (28.90, 19.60), (29.43, 19.075), 'R1.1']),
    ('GATE', F, TRACK, ['R1.2', 'Q1.1', 'R4.2']),
    ('PERIPH_EN_N', F, TRACK, ['U1.3', (40.30, 23.22), (39.35, 22.27), 'R4.1']),
    ('MOIST_ADC', F, TRACK, ['R5.2', 'C4.1', 'U1.2']),
    # 3V3_SW: Q1 drain down to the bottom, west through the left-row and J4
    # gaps to J2, then up to the local bypass caps, J3.8 and TP3.
    ('3V3_SW', F, 0.3, ['Q1.3', (33.80, 23.22)]),
    ('3V3_SW', B, 0.3, [(33.80, 23.22), (32.53, 24.49), (25.90, 24.49), (24.48, 23.07),
                        (22.30, 23.07), (20.92, 24.45), (8.30, 24.45), 'J2.1']),
    ('3V3_SW', B, 0.3, [(8.30, 24.45), (7.175, 24.45)]),
    ('3V3_SW', F, 0.3, [(7.175, 24.20), 'C3.1', 'C2.1', (5.26, 20.90), 'J3.8']),
    ('3V3_SW', F, 0.3, [(11.175, 24.20), 'C5.1', (11.175, 21.00), 'TP3.1']),
    # Moisture probe signal: bottom, then up beside R5.
    ('MOIST_OUT', B, TRACK, ['J2.2', (10.30, 27.60), (25.00, 27.60), (25.57, 27.03),
                             (34.90, 27.03), (34.90, 25.76)]),
    ('MOIST_OUT', F, TRACK, [(34.90, 25.76), 'R5.1']),
    # Ground stubs to vias; the planes do the rest.
    ('GND', F, 0.3, ['C2.2', (10.00, 21.825), 'C3.2']),
    ('GND', F, 0.3, ['C5.2', (13.90, 22.75)]),
    ('GND', F, 0.3, ['C4.2', (41.30, 27.20)]),
    ('GND', F, 0.3, ['R3.2', (38.00, 31.45)]),
    ('GND', F, 0.3, ['C1.2', (40.00, 31.45)]),
    ('GND', F, 0.3, ['TP2.1', (12.00, 7.40)]),
]
VIAS = [
    ('3V3_SW', (33.80, 23.22)), ('3V3_SW', (7.175, 24.20)), ('3V3_SW', (11.175, 24.20)),
    ('MOIST_OUT', (34.90, 25.76)),
    ('GND', (10.00, 21.825)), ('GND', (13.90, 22.75)), ('GND', (41.30, 27.20)),
    ('GND', (38.00, 31.45)), ('GND', (40.00, 31.45)), ('GND', (12.00, 7.40)),
    # Plane stitching
    ('GND', (8.00, 3.00)), ('GND', (20.00, 3.00)), ('GND', (8.00, 9.00)),
    ('GND', (20.00, 9.00)), ('GND', (14.50, 30.50)), ('GND', (22.00, 30.50)),
    ('GND', (31.50, 25.80)),   # ties the top-plane island beside U1.13 down
]


def route(board, fps, nets):
    def where(p):
        if isinstance(p, str):
            ref, num = p.split('.')
            pad = next(x for x in fps[ref].Pads() if x.GetNumber() == num)
            return pad.GetPosition()
        return pt(*p)
    def net_of(name):
        # Local schematic labels export as '/NAME'.
        return nets.get(name) or nets['/' + name]
    for net, layer, width, points in ROUTES:
        for a, b in zip(points, points[1:]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(where(a)); t.SetEnd(where(b))
            t.SetWidth(mm(width)); t.SetLayer(layer); t.SetNet(net_of(net))
            board.Add(t)
    for net, p in VIAS:
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pt(*p))
        v.SetDrill(mm(VIA_DRILL)); v.SetWidth(F, mm(VIA_D))
        v.SetLayerPair(F, B); v.SetNet(net_of(net))
        board.Add(v)


# ── Planes and keep-outs ─────────────────────────────────────────────────────
def polygon(zone, x0, y0, x1, y1):
    o = zone.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(pt(x, y))


def zones(board, nets):
    for layer in (F, B):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNet(nets['/GND'] if '/GND' in nets else nets['GND'])
        polygon(z, 0, 0, W, H)
        z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25))
        z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        board.Add(z)
    for box, everything in ((ANTENNA_KEEPOUT, True), (ANTENNA_NO_POUR, False)):
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        ls = pcbnew.LSET(); ls.AddLayer(F); ls.AddLayer(B)
        z.SetLayerSet(ls)
        polygon(z, *box)
        z.SetDoNotAllowZoneFills(True)
        z.SetDoNotAllowTracks(everything)
        z.SetDoNotAllowVias(everything)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        board.Add(z)


def silk(board):
    def text(s, x, y, size=0.8, layer=pcbnew.F_SilkS, angle=0):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(angle)
        if layer == pcbnew.B_SilkS:
            t.SetMirrored(True)
        board.Add(t)
    text('PLANTER XIAO C6/S3', 16.0, 1.6, 1.0)
    text('REV B  2026-10', 16.0, 3.2)
    text('ANTENNA KEEP-OUT', 35.7, 5.0)
    text('NO COPPER', 35.7, 6.4)
    text('GND', 12.0, 4.3)
    text('PACK+', 20.6, 17.1)
    text('3V3SW', 12.0, 18.2)
    text('BAT+', 23.8, 19.2)
    text('BAT-', 23.8, 26.9)
    text('I-LINK', 15.2, 20.6, angle=90)
    text('PROBE', 5.2, 26.2, angle=90)
    text('+', 17.4, 24.1)
    text('-', 19.4, 24.1)
    text('BATT', 21.3, 22.6, angle=90)
    text('EPD', 3.2, 23.4)
    text('WEACT 1.54 BELOW', 14.0, 7.5, 1.0, layer=pcbnew.B_SilkS)
    text('J3 PIN 1', 4.6, 10.3, layer=pcbnew.B_SilkS)

if __name__ == '__main__':
    build()
