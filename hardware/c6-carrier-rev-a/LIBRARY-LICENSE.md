# Project symbol library attribution

The R, C, Q_PMOS_GSD, Conn_01x02, Conn_01x03, Conn_02x04_Odd_Even,
TestPoint and PWR_FLAG symbols in `Planter.kicad_sym` are unmodified extracts
from the KiCad 10 standard libraries, compiled by the KiCad community.

Source: [KiCad symbol libraries](https://gitlab.com/kicad/libraries/kicad-symbols).
These extracts remain licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/legalcode), with
the [KiCad libraries exception and warranty notice](https://gitlab.com/kicad/libraries/kicad-symbols/-/blob/master/LICENSE.md).
The exception allows electronic designs and generated design files using these
symbols to retain their own licensing; redistribution of a library collection
remains under the library licence. The library is provided without warranty.

The new `XIAO_ESP32C6_Carrier` symbol is a project-specific functional capture
with GPIO directions fixed for this application, not a verified PCB footprint.
Its pin mapping is based on the official Seeed module schematic linked in the
README. Its inclusion here does not establish suitability for fabrication.
