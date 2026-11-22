#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
# This example is also yours under CC0: copy it into your own work without credit.
"""Write a flange drawing as a Musa CAD .musa file.

    python3 flange.py flange.musa

Change the numbers below and run it again: the drawing follows. Then check it and print
it to PDF with Musa CAD:

    musacad --check flange.musa
    musacad --plot flange.musa flange.pdf --paper A4 --landscape --fit

The README preview is this PDF rendered with Poppler:

    pdftoppm -png -singlefile -scale-to 1320 flange.pdf automation-flange
"""
import math
import sys

# The part, in millimetres.
OUTER_DIAMETER = 120.0
BORE_DIAMETER = 40.0
BOLT_CIRCLE_DIAMETER = 90.0
BOLT_HOLE_DIAMETER = 10.0
BOLT_COUNT = 6

# Every object ends with the same seven properties: layer 0, colour, linetype and
# lineweight from the layer (ByLayer).
BY_LAYER = "0 7 255 255 255 0 25"


def line(x0, y0, x1, y1):
    return f"LINE {x0:g} {y0:g} {x1:g} {y1:g} {BY_LAYER}"


def circle(cx, cy, r):
    return f"CIRCLE {cx:g} {cy:g} {r:g} {BY_LAYER}"


def text(x, y, height, content):
    # A text is three lines: the record, the words, and the font (empty: the built-in font).
    return f"TEXT {x:g} {y:g} {height:g} 0 0 {BY_LAYER}\n{content}\n"


def linear_dimension(x0, y0, x1, y1, line_x, line_y):
    # Kind 0 is a linear dimension. Musa CAD measures it from the two points.
    return f"DIM 0 {x0:g} {y0:g} {x1:g} {y1:g} {line_x:g} {line_y:g} 0 {BY_LAYER}"


def drawing():
    records = [
        "MUSACAD 14",  # the format version this file is written in
        "UNITS mm",
        "CURRENT 0",
        "LTSCALE 1",
        "LAYER 255 255 255 0 25 1 0 0 0",  # layer 0: white, continuous, 0.25 mm, on
        "DIMSTYLE 2.5 2.5 0 0.6 1.25 2 1 25 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 Standard",
    ]
    r_out = OUTER_DIAMETER / 2
    records.append(circle(0, 0, r_out))
    records.append(circle(0, 0, BORE_DIAMETER / 2))
    for i in range(BOLT_COUNT):
        a = 2 * math.pi * i / BOLT_COUNT
        r = BOLT_CIRCLE_DIAMETER / 2
        records.append(circle(r * math.cos(a), r * math.sin(a), BOLT_HOLE_DIAMETER / 2))
    # Centre lines, a little longer than the part.
    ext = r_out + 6
    records.append(line(-ext, 0, ext, 0))
    records.append(line(0, -ext, 0, ext))
    # The overall width, measured across the outer circle.
    records.append(linear_dimension(-r_out, 0, r_out, 0, 0, -r_out - 15))
    records.append(text(-r_out, r_out + 12, 5, f"FLANGE, {BOLT_COUNT} HOLES ON PCD {BOLT_CIRCLE_DIAMETER:g}"))
    records.append("END")
    return "\n".join(records) + "\n"


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "flange.musa"
    with open(out, "w", encoding="utf-8") as f:
        f.write(drawing())
    print(f"wrote {out}")
