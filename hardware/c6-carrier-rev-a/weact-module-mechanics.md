# WeAct 1.54-inch e-paper module mechanical record

Source files supplied by Mark:

- `WeAct-EpaperModule-1.54 Board Shape 外形.pdf`
- `WeAct-EpaperModule-1.54 Board 3D.step`

The PDF was visually inspected across all three pages. Page 1 is the controlling
dimension drawing; pages 2 and 3 confirm component-side and reverse-side
orientation.

## Dimensions captured from the drawing

| Feature | Drawing value |
|---|---:|
| Maximum width | 50.12mm |
| Maximum height | 32.60mm |
| Horizontal hole-centre spacing | 44.52mm |
| Vertical hole-centre spacing | 27.00mm |
| Mounting-hole diameter | 3.00mm |
| Mounting-hole radius | 1.50mm |
| Header format | 2x4, 2.54mm pitch |

The 3.60mm annotations refer to the inset rectangular feature, and 1.93mm
locates the header relative to the left board edge. They are not mounting-hole
edge offsets. Symmetric hole offsets inferred from the overall and centre
dimensions are (50.12-44.52)/2 = 2.80mm horizontally and
(32.60-27.00)/2 = 2.80mm vertically. Confirm against STEP before modelling.
The right-hand outline is narrowed/chamfered; use STEP for collision checks.

## Header order

Viewed from the component side, with the printed legend readable and the 2x4
header on the left:

```text
top
  1 BUSY   2 RES
  3 D/C    4 CS
  5 SCL    6 SDA
  7 GND    8 VCC
bottom
```

The carrier connector and any IDC cable must preserve this numbering. Confirm
continuity on the first cable before applying power; a rotated 2x4 connector
swaps signal and power positions.
