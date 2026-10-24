"""Build the Rev B board from the Rev B schematic. Run with KiCad's own Python:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 make_pcb.py

Nets, references, values, footprints and symbol links all come from the
schematic's exported netlist, so DRC schematic parity holds by construction.
Placement and routing are explicit below: board coordinates in mm from the top
left corner, top view, USB-C at the bottom edge. The XIAO ESP32-S3 sits flat on
its castellations at the left, beside the display header; everything else is
to its right or in the band above it.

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
if KICAD.exists():                                   # macOS bundle
    KICAD_CLI = str(KICAD / 'MacOS/kicad-cli')
    FP_LIBS = KICAD / 'SharedSupport/footprints'
else:                                                # Linux package
    KICAD_CLI = 'kicad-cli'
    FP_LIBS = Path('/usr/share/kicad/footprints')
ORIGIN = (100.0, 100.0)          # sheet position of the board's top-left corner
W, H = 50.12, 32.60              # WeAct 1.54-inch module outline
HOLES = [(2.80, 2.80), (47.32, 2.80), (2.80, 29.80), (47.32, 29.80)]  # 44.52 x 27.00
NOTCH = (46.97, 6.30, 8.80, 23.80, 26.30)   # FPC fold recess on the right edge
CORNER_R = 2.5
XIAO = (16.50, 20.68)            # centre of the 2x7 castellation field
XIAO_BODY = (8.9, 10.5)          # half-size of the 17.8 x 21.0 module
# Underside contact areas of the S3 (module-relative, Seeed XIAO-ESP32-S3-SMD
# rotated 180 deg): JTAG/EN/USB pads, the large unconnected pad, and BAT+/-.
# No vias under these; tracks there are masked, but kept out anyway.
XIAO_UNDERSIDE_KEEPOUT = (-2.8, -3.4, 6.0, 9.6)

TRACK, POWER = 0.25, 0.5
VIA_D, VIA_DRILL = 0.6, 0.3


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y))


# ── Custom footprint ─────────────────────────────────────────────────────────
def write_xiao_footprint():
    """Seeed XIAO ESP32-S3 soldered flat by its castellations, USB toward +y.

    Land pattern from Seeed's own XIAO-ESP32-S3-SMD footprint (OPL KiCad
    library), rotated 180 deg so USB-C points at the board's bottom edge:
    14 castellation pads 2.75 x 2.0 mm on 16.165 mm row centres, 2.54 pitch.
    Pad numbers match the castellations: 1-7 = D0-D6, 8-11 = D7-D10, 12 3V3,
    13 GND, 14 5V. Pad 23 is the module's underside BAT+ contact, plated
    through so it can be soldered from the back of the carrier with an iron.
    The module's BAT- pad is its own GND, returned through pad 13, so it gets
    no copper: that leaves no hidden joint that could short the battery. Nor
    do the S3's other underside pads (JTAG, EN, USB, the large centre pad).
    """
    hw, hh = XIAO_BODY
    pads = [(n, 8.0825, 7.62 - (n - 1) * 2.54) for n in range(1, 8)]
    pads += [(n, -8.0825, -7.62 + (n - 8) * 2.54) for n in range(8, 15)]
    lines = ['(footprint "Seeed_XIAO_ESP32S3_SMD" (version 20241229) (generator "planter_capture")',
             '  (layer "F.Cu")',
             '  (descr "Seeed XIAO ESP32-S3 surface-mounted by castellations, '
             'underside BAT+ pad plated through for hand soldering, USB-C toward +y")',
             '  (attr smd)',
             '  (property "Reference" "U**" (at 0 -1.5 0) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))',
             '  (property "Value" "XIAO" (at 0 1.5 0) (layer "F.Fab") (effects (font (size 0.8 0.8) (thickness 0.12))))']
    def line(layer, a, b, w=0.12):
        lines.append(f'  (fp_line (start {a[0]} {a[1]}) (end {b[0]} {b[1]}) (stroke (width {w}) (type solid)) (layer "{layer}"))')
    def rect(layer, x0, y0, x1, y1, w=0.12):
        for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            line(layer, a, b, w)
    rect('F.Fab', -hw, -hh, hw, hh, 0.1)
    rect('F.Fab', -4.5, 9.0, 4.5, 11.92, 0.1)             # USB-C receptacle
    # Silk outline only between the castellation rows (top and bottom ends).
    for y in (-hh - 0.12, hh + 0.12):
        line('F.SilkS', (-6.6, y), (6.6, y))
    rect('F.CrtYd', -9.71, -hh - 0.25, 9.71, hh + 0.25, 0.05)
    lines.append('  (fp_circle (center 10.3 7.62) (end 10.55 7.62) (stroke (width 0.12) (type solid)) (fill solid) (layer "F.SilkS"))')
    for text, y in (('USB', 9.3),):
        lines.append(f'  (fp_text user "{text}" (at 0 {y} 0) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))')
    lines.append('  (fp_text user "BAT+" (at 4.47 -0.75 0) (layer "F.Fab") (effects (font (size 0.5 0.5) (thickness 0.08))))')
    for n, x, y in pads:
        lines.append(f'  (pad "{n}" smd roundrect (at {x} {y}) (size 2.75 2.0) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))')
    lines.append('  (pad "23" thru_hole roundrect (at 4.4725 0.382) (size 2.5 1.25) (drill 0.7) (layers "*.Cu" "*.Mask") (roundrect_rratio 0.25))')
    lines.append(')')
    lib = ROOT / 'Planter.pretty'
    lib.mkdir(exist_ok=True)
    for old in lib.glob('*.kicad_mod'):
        old.unlink()
    (lib / 'Seeed_XIAO_ESP32S3_SMD.kicad_mod').write_text('\n'.join(lines) + '\n')
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
    subprocess.run([KICAD_CLI, 'sch', 'export', 'netlist',
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
    'J2': (29.60, 25.70, 0, False),
    'J1': (39.30, 25.70, 0, False),
    'JP1': (39.30, 21.40, 180, False),
    'TP1': (36.00, 14.20, 0, False),
    'TP2': (9.00, 3.00, 0, False),
    'TP3': (44.60, 11.80, 0, False),
    'C2': (3.00, 22.70, 180, False),
    'C3': (3.00, 24.60, 180, False),
    'C5': (35.90, 22.30, 0, False),
    'Q1': (31.20, 4.50, 0, False),
    'R1': (28.30, 4.50, 90, False),
    'R4': (25.60, 3.60, 0, False),
    'R2': (30.60, 14.20, 180, False),
    'C1': (30.60, 16.00, 0, False),
    'R3': (30.60, 17.80, 0, False),
    'C4': (29.00, 22.30, 0, False),
    'R5': (32.20, 22.30, 180, False),
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
    # Vias are tented on both sides: none sit under the module's underside
    # pads, but nothing near it should be bare copper either.
    ds = board.GetDesignSettings()
    ds.m_TentViasFront = True
    ds.m_TentViasBack = True
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
    # Display bus. DC/CS/SCLK/MOSI go straight across to the XIAO's left row,
    # the outer-column signals threading the gaps between J3's inner pins.
    ('EPD_DC', F, TRACK, ['J3.3', (3.20, 13.76), (6.40, 13.76), 'U1.8']),
    ('EPD_CS', F, TRACK, ['J3.4', 'U1.9']),
    ('EPD_SCLK', F, TRACK, ['J3.5', (3.20, 16.30), (6.00, 16.30), (7.84, 18.14), 'U1.10']),
    ('EPD_MOSI', F, TRACK, ['J3.6', (5.75, 17.57), (5.75, 19.90), (6.53, 20.68), 'U1.11']),
    # BUSY/RST run under the module's top end (no underside pads there) to the
    # right row.
    ('EPD_BUSY', F, TRACK, ['J3.1', (1.93, 11.00), (27.30, 11.00), (27.30, 18.14), 'U1.5']),
    ('EPD_RST', F, TRACK, ['J3.2', (5.40, 11.55), (26.60, 11.55), (26.60, 15.60), 'U1.6']),
    # Battery: J1 -> JP1 current link -> PACK+. Bottom side: down the band and
    # under the module to its underside BAT+ pad. Top side: TP1 and the divider.
    ('PACK_IN+', F, POWER, ['J1.1', 'JP1.1']),
    ('PACK+', B, POWER, ['U1.23', (20.9725, 9.40), (40.00, 9.40), (40.00, 18.16), 'JP1.2']),
    ('PACK+', F, 0.3, ['JP1.2', (39.30, 14.20), 'TP1.1', 'R2.1']),
    ('VBAT_ADC', F, TRACK, ['U1.4', (28.90, 20.68), (28.90, 14.20), 'R2.2']),
    ('VBAT_ADC', F, TRACK, [(28.90, 16.00), 'C1.1']),
    ('VBAT_ADC', F, TRACK, [(28.90, 17.80), 'R3.1']),
    ('MOIST_ADC', F, TRACK, ['U1.3', (27.40, 23.22), (30.90, 23.22), (31.375, 22.745), 'R5.2']),
    ('MOIST_ADC', F, TRACK, [(28.175, 23.22), 'C4.1']),
    ('MOIST_OUT', F, TRACK, ['J2.2', (33.025, 24.275), 'R5.1']),
    # 3V3: left-row pad, down to the bottom, up the left edge and along the
    # band to the switch.
    ('3V3', F, 0.3, ['U1.12', (6.45, 23.22)]),
    ('3V3', B, 0.3, [(6.45, 23.22), (6.45, 8.00), (29.20, 8.00)]),
    ('3V3', F, 0.3, [(29.20, 8.00), (29.20, 6.30)]),
    ('3V3', F, 0.3, ['R1.1', (28.30, 6.30), (30.2625, 6.30), 'Q1.2']),
    # Peripheral switch in the band. PERIPH_EN_N leaves D1 by a via beside it.
    ('PERIPH_EN_N', F, TRACK, ['U1.2', (26.65, 25.76)]),
    ('PERIPH_EN_N', B, TRACK, [(26.65, 25.76), (26.65, 10.20)]),
    ('PERIPH_EN_N', F, TRACK, [(26.65, 10.20), (24.775, 8.40), 'R4.1']),
    ('GATE', F, TRACK, ['R4.2', 'R1.2', 'Q1.1']),
    # 3V3_SW: Q1 drain to a bottom-side ring: west along the band and down to
    # J3.8, east and down the right side to TP3, C5 and J2.1.
    ('3V3_SW', F, 0.3, ['Q1.3', (33.20, 4.50), (33.20, 6.00)]),
    ('3V3_SW', B, 0.3, ['J3.8', (5.75, 19.40), (5.75, 6.00), (43.50, 6.00), (43.50, 23.30),
                        (42.50, 24.30), (30.10, 24.30), 'J2.1']),
    ('3V3_SW', F, 0.3, ['J3.8', (3.95, 21.40), 'C2.1', 'C3.1']),
    ('3V3_SW', F, 0.3, [(43.50, 11.80), 'TP3.1']),
    ('3V3_SW', F, 0.3, ['C5.1', (35.075, 23.30)]),
    ('3V3_SW', B, 0.3, [(35.075, 23.30), (35.075, 24.30)]),
    # Ground stubs to vias; the planes do the rest.
    ('GND', F, 0.3, ['C2.2', (1.10, 23.65), 'C3.2']),
    ('GND', F, 0.3, ['C1.2', (32.50, 16.90), 'R3.2']),
    ('GND', F, 0.3, ['C4.2', (29.825, 21.20)]),
    ('GND', F, 0.3, ['C5.2', (36.725, 23.40)]),
]
VIAS = [
    ('3V3', (6.45, 23.22)), ('3V3', (29.20, 8.00)),
    ('PERIPH_EN_N', (26.65, 25.76)), ('PERIPH_EN_N', (26.65, 10.20)),
    ('3V3_SW', (33.20, 6.00)), ('3V3_SW', (43.50, 11.80)), ('3V3_SW', (35.075, 23.30)),
    ('GND', (1.10, 23.65)), ('GND', (32.50, 16.90)), ('GND', (29.825, 21.20)),
    ('GND', (36.725, 23.40)),
    # Plane stitching
    ('GND', (14.00, 6.80)), ('GND', (22.00, 6.80)), ('GND', (37.00, 3.00)),
    ('GND', (42.00, 3.00)), ('GND', (36.00, 17.50)), ('GND', (44.50, 20.50)),
    ('GND', (2.00, 7.50)),
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
    # Under the S3's own underside pads: no top-side tracks or vias.
    x0, y0, x1, y1 = XIAO_UNDERSIDE_KEEPOUT
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetLayer(F)
    polygon(z, XIAO[0] + x0, XIAO[1] + y0, XIAO[0] + x1, XIAO[1] + y1)
    z.SetDoNotAllowZoneFills(False)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
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
    text('PLANTER XIAO S3', 19.0, 1.6, 1.0)
    text('REV B  2026-10', 16.0, 3.4)
    text('GND', 9.0, 4.7)
    text('PACK+', 36.0, 12.6)
    text('3V3SW', 44.4, 13.6)
    text('I-LINK', 41.7, 20.1, angle=90)
    text('+', 39.3, 23.4)
    text('-', 41.3, 23.4)
    text('BATT', 43.9, 22.4)
    text('PROBE', 26.75, 30.55, angle=90)
    text('EPD', 3.2, 9.4)
    text('U.FL ANT', 16.5, 8.6)
    text('WEACT 1.54 BELOW', 14.0, 7.5, 1.0, layer=pcbnew.B_SilkS)
    text('J3 PIN 1', 4.6, 10.3, layer=pcbnew.B_SilkS)
    text('BAT+', 17.6, 21.06, layer=pcbnew.B_SilkS)
    text('SOLDER FROM HERE', 21.0, 23.0, layer=pcbnew.B_SilkS)

if __name__ == '__main__':
    build()
