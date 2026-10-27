#!/bin/sh
# JLCPCB-style outputs into build/fab: Gerbers + drill (zipped), BOM, placement.
set -e
cd "$(dirname "$0")"
CLI=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
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
ls -1 "$OUT"
