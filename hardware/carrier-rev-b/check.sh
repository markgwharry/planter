#!/bin/sh
# Regenerate schematic + board, then fail on any ERC/DRC/parity finding.
set -e
cd "$(dirname "$0")"
KICAD=/Applications/KiCad/KiCad.app/Contents
CLI=$KICAD/MacOS/kicad-cli
PY=$KICAD/Frameworks/Python.framework/Versions/Current/bin/python3
python3 make_kicad.py
"$PY" make_pcb.py 2>&1 | grep -v -e wxApp -e stdpbase
"$CLI" sch erc --severity-all --exit-code-violations -o erc.rpt planter-carrier-rev-b.kicad_sch
mkdir -p build
"$CLI" pcb drc --schematic-parity --severity-all --refill-zones --exit-code-violations \
    -o build/drc.rpt planter-carrier-rev-b.kicad_pcb
echo "PASS: ERC clean, DRC clean, schematic parity clean."
