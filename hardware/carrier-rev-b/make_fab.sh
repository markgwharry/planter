#!/bin/sh
# JLCPCB-style outputs into build/fab: Gerbers + drill (zipped), BOM, placement.
set -e
cd "$(dirname "$0")"
KICAD=/Applications/KiCad/KiCad.app/Contents
CLI=$KICAD/MacOS/kicad-cli
PY=$KICAD/Frameworks/Python.framework/Versions/Current/bin/python3
B=planter-carrier-rev-b
OUT=build/fab
rm -rf "$OUT"; mkdir -p "$OUT/gerbers"
"$CLI" pcb export gerbers --no-protel-ext -o "$OUT/gerbers/" \
    --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts $B.kicad_pcb
"$CLI" pcb export drill --format excellon --excellon-separate-th -o "$OUT/gerbers/" $B.kicad_pcb
(cd "$OUT/gerbers" && zip -q ../$B-gerbers.zip *)
# Full BOM: every fitted part, including the hand-fitted ones (no LCSC number).
# Every designator is listed in full: JLCPCB does not read ranges like C3-C6.
"$CLI" sch export bom --fields 'Value,Reference,Footprint,${QUANTITY},LCSC' \
    --labels 'Comment,Designator,Footprint,Quantity,LCSC Part #' \
    --group-by Value,Footprint,LCSC --ref-range-delimiter '' --exclude-dnp \
    -o "$OUT/$B-bom-full.csv" $B.kicad_sch
# JLCPCB BOM: only parts with an LCSC number. The XIAO, its sockets, J3, J4
# and JP1 are hand-fitted (see README).
grep -v ',""$' "$OUT/$B-bom-full.csv" > "$OUT/$B-bom.csv"
# JLCPCB CPL: top side, JLCPCB's column names, and only the designators in the
# JLCPCB BOM. Check its placement preview: SOT-23/SOT-23-6 rotations
# sometimes need correcting there.
"$CLI" pcb export pos --side front --format csv --units mm --exclude-dnp \
    -o "$OUT/cpl-all.csv" $B.kicad_pcb
awk -F'","' 'NR == FNR { if (FNR > 1) { n = split($2, r, ","); for (i = 1; i <= n; i++) keep[r[i]] = 1 }; next }
     FNR == 1 { print "Designator,Val,Package,Mid X,Mid Y,Rotation,Layer"; next }
     { split($0, f, ","); d = f[1]; gsub(/"/, "", d); if (d in keep) print }' \
    "$OUT/$B-bom.csv" "$OUT/cpl-all.csv" > "$OUT/$B-cpl-top.csv"
rm "$OUT/cpl-all.csv"
# JLCPCB centres each part on its CPL coordinate, but KiCad exports a
# through-hole footprint's anchor, which for the JST connectors is pin 1.
# Move through-hole parts to the centre of their pins.
"$PY" - $B.kicad_pcb "$OUT/$B-cpl-top.csv" <<'EOF'
import sys, pcbnew
board, cpl = sys.argv[1], sys.argv[2]
centre = {}
for fp in pcbnew.LoadBoard(board).GetFootprints():
    pads = list(fp.Pads())
    if pads and all(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in pads):
        xs = [pcbnew.ToMM(p.GetPosition().x) for p in pads]
        ys = [pcbnew.ToMM(p.GetPosition().y) for p in pads]
        centre[fp.GetReference()] = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
lines = open(cpl).read().splitlines()
for i, line in enumerate(lines[1:], 1):
    f = line.split(',')
    ref = f[0].strip('"')
    if ref in centre:
        f[3], f[4] = '%.6f' % centre[ref][0], '%.6f' % -centre[ref][1]
        lines[i] = ','.join(f)
        print('CPL: %s moved to its pin centre' % ref)
open(cpl, 'w').write('\n'.join(lines) + '\n')
EOF
ls -1 "$OUT"
