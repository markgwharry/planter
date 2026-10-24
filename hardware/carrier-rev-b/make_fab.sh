#!/bin/sh
# JLCPCB-style outputs into build/fab: Gerbers + drill (zipped), BOM, placement.
set -e
cd "$(dirname "$0")"
CLI=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
[ -x "$CLI" ] || CLI=kicad-cli
B=planter-carrier-rev-b
OUT=build/fab
rm -rf "$OUT"; mkdir -p "$OUT/gerbers"
"$CLI" pcb export gerbers --no-protel-ext -o "$OUT/gerbers/" \
    --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts $B.kicad_pcb
"$CLI" pcb export drill --format excellon --excellon-separate-th -o "$OUT/gerbers/" $B.kicad_pcb
(cd "$OUT/gerbers" && zip -q ../$B-gerbers.zip *)
"$CLI" pcb export pos --side front --format csv --units mm --exclude-dnp -o "$OUT/$B-cpl-top.csv" $B.kicad_pcb
"$CLI" sch export bom --fields 'Reference,Value,Footprint,${QUANTITY}' \
    --group-by Value,Footprint --exclude-dnp -o "$OUT/$B-bom.csv" $B.kicad_sch
ls -1 "$OUT"
