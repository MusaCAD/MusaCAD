#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
"""Write the drawings the listing screenshots show, as DXF files in <out_dir>:

    residence.dxf        a single-storey house: floor plan, south elevation, room schedule
    flange.dxf           a dimensioned flange: front view, section A-A, GD&T, title block
    footing-detail.dxf   a wall / strip-footing construction section with its materials

Every line of them is made here, so the screenshots carry no third-party drawing.

    tools/screenshots/make_drawings.py <out_dir>
"""
import math
import os
import sys

# ---------------------------------------------------------------------------
# A small DXF writer: just what these drawings use, in the form Musa CAD reads.
# ---------------------------------------------------------------------------


def num(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def rgb_int(c):
    return (c[0] << 16) | (c[1] << 8) | c[2]


class Dxf:
    def __init__(self, ltscale=1.0):
        self.ltscale = ltscale
        self.layers = []
        self.dimstyles = []
        self.ents = []

    # --- tables ---
    def layer(self, name, color, ltype="CONTINUOUS", lw=25):
        self.layers.append((name, color, ltype, lw))

    def dimstyle(self, name, text, arrow, prec=0):
        self.dimstyles.append((name, text, arrow, prec))

    # --- entities ---
    def _add(self, kind, layer, pairs, color=None):
        e = [(0, kind), (8, layer)]
        if color is not None:
            e.append((420, rgb_int(color)))
        e.extend(pairs)
        self.ents.append(e)

    def line(self, a, b, layer, color=None):
        self._add("LINE", layer, [(10, a[0]), (20, a[1]), (30, 0), (11, b[0]), (21, b[1]), (31, 0)], color)

    def poly(self, pts, layer, closed=False, width=None, bulges=None, color=None):
        pairs = [(90, len(pts)), (70, 1 if closed else 0)]
        if width:
            pairs.append((43, width))
        for i, p in enumerate(pts):
            pairs += [(10, p[0]), (20, p[1])]
            if bulges and bulges[i]:
                pairs.append((42, bulges[i]))
        self._add("LWPOLYLINE", layer, pairs, color)

    def rect(self, x0, y0, x1, y1, layer, width=None, color=None):
        self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], layer, True, width, color=color)

    def circle(self, c, r, layer, color=None):
        self._add("CIRCLE", layer, [(10, c[0]), (20, c[1]), (30, 0), (40, r)], color)

    def arc(self, c, r, a0, a1, layer, color=None):
        """Counter-clockwise from a0 to a1, in degrees."""
        self._add("ARC", layer, [(10, c[0]), (20, c[1]), (30, 0), (40, r), (50, a0), (51, a1)], color)

    def ellipse(self, c, major, ratio, layer, color=None):
        self._add("ELLIPSE", layer, [(10, c[0]), (20, c[1]), (30, 0), (11, major[0]), (21, major[1]),
                                     (31, 0), (40, ratio), (41, 0), (42, 2 * math.pi)], color)

    def text(self, p, h, s, layer, just=0, rot=0.0, color=None):
        """just: 0 left, 1 centred, 2 right -- on the point given."""
        pairs = [(10, p[0]), (20, p[1]), (30, 0), (40, h), (1, s), (50, rot), (72, just)]
        if just:
            pairs += [(11, p[0]), (21, p[1]), (31, 0)]
        self._add("TEXT", layer, pairs, color)

    def mtext(self, p, h, lines, layer, width=0.0, color=None):
        pairs = [(10, p[0]), (20, p[1]), (30, 0), (40, h), (41, width), (71, 1), (1, "\\P".join(lines))]
        self._add("MTEXT", layer, pairs, color)

    def hatch(self, loops, pattern, layer, scale=1.0, angle=0.0, color=None):
        solid = pattern == "SOLID"
        pairs = [(10, 0), (20, 0), (30, 0), (210, 0), (220, 0), (230, 1), (2, pattern),
                 (70, 1 if solid else 0), (71, 0), (91, len(loops))]
        for loop in loops:
            pairs += [(92, 2), (72, 0), (73, 1), (93, len(loop))]
            for p in loop:
                pairs += [(10, p[0]), (20, p[1])]
            pairs.append((97, 0))
        pairs += [(75, 0), (76, 1)]
        if not solid:
            pairs += [(52, angle), (41, scale), (77, 0)]
        pairs.append((98, 0))
        self._add("HATCH", layer, pairs, color)

    def dim(self, kind, a, b, place, layer, style):
        """kind: 0 linear, 1 aligned, 3 diameter, 4 radius, 5 angular. Linear runs along
        the axis the two points differ most on; radius / diameter take the centre and a
        point on the curve; angular the vertex and a point on each ray."""
        self._add("DIMENSION", layer, [(2, "*D0"), (3, style), (10, place[0]), (20, place[1]), (30, 0),
                                       (11, place[0]), (21, place[1]), (31, 0), (70, kind),
                                       (13, a[0]), (23, a[1]), (33, 0), (14, b[0]), (24, b[1]), (34, 0)])

    def leader(self, tip, knee, layer, style):
        self._add("LEADER", layer, [(3, style), (71, 1), (76, 2), (10, tip[0]), (20, tip[1]), (30, 0),
                                    (10, knee[0]), (20, knee[1]), (30, 0)])

    def tolerance(self, p, cells, layer, style):
        self._add("TOLERANCE", layer, [(3, style), (10, p[0]), (20, p[1]), (30, 0), (1, "%%v".join(cells)),
                                       (11, 1), (21, 0), (31, 0)])

    # --- file ---
    def save(self, path):
        out = []

        def put(code, value):
            out.append(str(code))
            out.append(num(value) if isinstance(value, (int, float)) and not isinstance(value, bool)
                       and code not in (62, 70, 71, 72, 73, 75, 76, 77, 90, 91, 92, 93, 97, 98, 370, 420)
                       else str(value))

        put(0, "SECTION"), put(2, "HEADER")
        put(9, "$ACADVER"), put(1, "AC1015")
        put(9, "$INSUNITS"), put(70, 4)
        put(9, "$LTSCALE"), put(40, self.ltscale)
        put(0, "ENDSEC")
        put(0, "SECTION"), put(2, "TABLES")
        put(0, "TABLE"), put(2, "LTYPE"), put(70, 4)
        for name, desc, elems in (("CONTINUOUS", "Solid line", []),
                                  ("DASHED", "Dashed __ __ __", [5.0, -2.5]),
                                  ("HIDDEN", "Hidden _ _ _", [2.5, -1.25]),
                                  ("CENTER", "Center ____ _ ____", [12.5, -2.5, 2.5, -2.5])):
            put(0, "LTYPE"), put(2, name), put(70, 0), put(3, desc), put(72, 65), put(73, len(elems))
            put(40, sum(abs(e) for e in elems))
            for e in elems:
                put(49, e)
        put(0, "ENDTAB")
        put(0, "TABLE"), put(2, "LAYER"), put(70, len(self.layers))
        for name, color, ltype, lw in self.layers:
            put(0, "LAYER"), put(2, name), put(70, 0), put(62, 7), put(420, rgb_int(color)), put(6, ltype), put(370, lw)
        put(0, "ENDTAB")
        put(0, "TABLE"), put(2, "DIMSTYLE"), put(70, len(self.dimstyles))
        for name, text, arrow, prec in self.dimstyles:
            put(0, "DIMSTYLE"), put(2, name), put(70, 0), put(140, text), put(41, arrow), put(271, prec)
        put(0, "ENDTAB")
        put(0, "ENDSEC")
        put(0, "SECTION"), put(2, "ENTITIES")
        for e in self.ents:
            for code, value in e:
                put(code, value)
        put(0, "ENDSEC")
        put(0, "EOF")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(out) + "\n")


# ---------------------------------------------------------------------------
# Shared pieces
# ---------------------------------------------------------------------------


def door(d, layer, hinge, wall_dir, swing_dir, width):
    """A door leaf standing open at 90 degrees and its swing to the far jamb."""
    leaf_end = (hinge[0] + swing_dir[0] * width, hinge[1] + swing_dir[1] * width)
    d.line(hinge, leaf_end, layer)
    a_leaf = math.degrees(math.atan2(swing_dir[1], swing_dir[0]))
    a_jamb = math.degrees(math.atan2(wall_dir[1], wall_dir[0]))
    cross = swing_dir[0] * wall_dir[1] - swing_dir[1] * wall_dir[0]
    if cross > 0:
        d.arc(hinge, width, a_leaf, a_jamb, layer)
    else:
        d.arc(hinge, width, a_jamb, a_leaf, layer)


def bubble(d, layer, text_layer, c, r, label, h):
    d.circle(c, r, layer)
    d.text((c[0], c[1] - h / 2), h, label, text_layer, just=1)


def level_mark(d, layer, x, y, label, size):
    d.poly([(x, y), (x - size * 0.6, y + size), (x + size * 0.6, y + size)], layer, closed=True)
    d.hatch([[(x, y), (x - size * 0.6, y + size), (x + size * 0.6, y + size)]], "SOLID", layer)
    d.line((x - size * 0.6, y), (x + size * 7, y), layer)
    d.text((x + size, y + size * 0.35), size * 0.9, label, layer)


# ---------------------------------------------------------------------------
# 1. The house: plan, south elevation, room schedule (mm, 1:100)
# ---------------------------------------------------------------------------


def residence(path):
    d = Dxf(ltscale=40)
    d.layer("A-WALL", (228, 228, 228), lw=35)
    d.layer("A-DOOR", (126, 204, 146), lw=18)
    d.layer("A-GLAZ", (116, 196, 236), lw=18)
    d.layer("A-FURN", (128, 132, 146), lw=13)
    d.layer("A-FLOR-PATT", (66, 72, 84), lw=9)
    d.layer("A-GRID", (214, 84, 84), "CENTER", lw=13)
    d.layer("A-GRID-IDEN", (214, 84, 84), lw=18)
    d.layer("A-ANNO-DIMS", (92, 196, 204), lw=13)
    d.layer("A-ANNO-TEXT", (236, 206, 112), lw=18)
    d.layer("A-ELEV", (228, 228, 228), lw=25)
    d.layer("A-ELEV-PATT", (150, 104, 86), lw=9)
    d.layer("A-ELEV-GLAZ", (52, 92, 128), lw=9)
    d.layer("A-ELEV-ROOF", (176, 110, 78), lw=9)
    d.layer("A-SITE", (112, 92, 72), lw=9)
    d.layer("A-TABL", (150, 154, 168), lw=13)
    d.dimstyle("ARCH", 270, 190, 0)

    W, H = 14000, 9000
    ext_t, int_t = 300, 120

    # Exterior wall: one closed centre line, cut where the windows and the entry are.
    ring = [(0, 0), (W, 0), (W, H), (0, H)]
    edges = [(ring[i], ring[(i + 1) % 4]) for i in range(4)]
    lengths = [math.dist(a, b) for a, b in edges]
    starts = [sum(lengths[:i]) for i in range(4)]
    total = sum(lengths)
    windows = {  # edge: [(from, to)] along the edge
        0: [(1000, 3400), (6200, 8300), (10700, 12800)],
        2: [(1200, 2200), (3800, 5400), (7100, 8100), (10800, 12800)],
        3: [(5200, 7500)],
    }
    entry = (1, 4100, 5100)
    openings = [(starts[e] + a, starts[e] + b) for e, spans in windows.items() for a, b in spans]
    openings.append((starts[entry[0]] + entry[1], starts[entry[0]] + entry[2]))
    openings.sort()

    def point_at(s):
        s %= total
        for i in range(4):
            if s <= starts[i] + lengths[i] + 1e-9:
                t = (s - starts[i]) / lengths[i]
                a, b = edges[i]
                return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        return ring[0]

    for i, (_, s1) in enumerate(openings):
        s0 = s1
        s1n = openings[(i + 1) % len(openings)][0] + (total if i + 1 == len(openings) else 0)
        pts = [point_at(s0)]
        for k in range(1, 9):
            corner = starts[k % 4] + (total if k >= 4 else 0)
            if s0 < corner < s1n:
                pts.append(ring[k % 4])
        pts.append(point_at(s1n))
        d.poly(pts, "A-WALL", width=ext_t)

    # Windows in the exterior wall: the wall faces and a double glass line.
    for e, spans in windows.items():
        a, b = edges[e]
        ux, uy = (b[0] - a[0]) / lengths[e], (b[1] - a[1]) / lengths[e]
        nx, ny = -uy, ux
        for s0, s1 in spans:
            p0 = (a[0] + ux * s0, a[1] + uy * s0)
            p1 = (a[0] + ux * s1, a[1] + uy * s1)
            for off in (-ext_t / 2, -25, 25, ext_t / 2):
                d.line((p0[0] + nx * off, p0[1] + ny * off), (p1[0] + nx * off, p1[1] + ny * off), "A-GLAZ")

    # Interior walls (centre lines, openings as (from, to) along them).
    def wall(a, b, gaps=()):
        L = math.dist(a, b)
        ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        cut = [0.0]
        for g0, g1 in gaps:
            cut += [g0, g1]
        cut.append(L)
        for k in range(0, len(cut), 2):
            s0, s1 = cut[k], cut[k + 1]
            d.poly([(a[0] + ux * s0, a[1] + uy * s0), (a[0] + ux * s1, a[1] + uy * s1)], "A-WALL", width=int_t)

    d.poly([(5000, 0), (5000, 4000), (7400, 4000)], "A-WALL", width=int_t)  # bedroom 1 / living, corridor
    wall((8300, 4000), (W, 4000), [(3500, 4400)])                            # corridor, bedroom 2 door
    wall((0, 5200), (W, 5200), [(2800, 4600), (5400, 6200), (8200, 9100), (11300, 12200)])
    wall((5000, 5200), (5000, H))
    wall((9500, 0), (9500, 4000))
    wall((7800, 5200), (7800, H))
    wall((11000, 5200), (11000, H))

    # Doors: hinge at the wall face, swinging into the room.
    fi = int_t / 2
    door(d, "A-DOOR", (7400, 4000 - fi), (1, 0), (0, -1), 900)       # bedroom 1
    door(d, "A-DOOR", (12700, 4000 - fi), (-1, 0), (0, -1), 900)     # bedroom 2
    door(d, "A-DOOR", (5400, 5200 + fi), (1, 0), (0, 1), 800)        # bath
    door(d, "A-DOOR", (9100, 5200 + fi), (-1, 0), (0, 1), 900)       # study
    door(d, "A-DOOR", (11300, 5200 + fi), (1, 0), (0, 1), 900)       # utility
    door(d, "A-DOOR", (W - ext_t / 2, 4100), (0, 1), (-1, 0), 1000)  # entry

    # Furniture.
    F = "A-FURN"
    # living
    d.rect(300, 1200, 1250, 3900, F)
    d.rect(300, 1200, 1250, 1450, F)
    d.rect(300, 3650, 1250, 3900, F)
    d.line((500, 2100), (1250, 2100), F)
    d.line((500, 3000), (1250, 3000), F)
    d.rect(1850, 1900, 2750, 3200, F)
    for y0 in (1000, 3350):
        d.rect(3300, y0, 4100, y0 + 800, F)
        d.rect(3300, y0, 4100, y0 + 180, F)
    d.rect(4500, 1300, 4850, 3800, F)
    # kitchen / dining
    d.rect(300, 8250, 4700, 8850, F)
    d.rect(1900, 8350, 2700, 8750, F)
    d.circle((2300, 8550), 60, F)
    for x in (3650, 4150):
        for y in (8420, 8680):
            d.circle((x, y), 110, F)
    d.rect(300, 7250, 1000, 8150, F)
    d.line((300, 7250), (1000, 8150), F)
    d.line((300, 8150), (1000, 7250), F)
    d.rect(1700, 6050, 3300, 6950, F)
    for x0 in (1900, 2575):
        d.rect(x0, 7020, x0 + 450, 7470, F)
        d.rect(x0, 5530, x0 + 450, 5980, F)
    d.rect(1180, 6275, 1630, 6725, F)
    d.rect(3370, 6275, 3820, 6725, F)
    # bedroom 1
    d.rect(5150, 1200, 7150, 2800, F)
    d.rect(5230, 1300, 5630, 1950, F)
    d.rect(5230, 2050, 5630, 2700, F)
    d.line((6150, 1200), (6150, 2800), F)
    d.rect(5150, 700, 5600, 1150, F)
    d.rect(5150, 2850, 5600, 3300, F)
    d.rect(8840, 600, 9440, 3400, F)
    d.line((8840, 2000), (9440, 2000), F)
    # bedroom 2
    d.rect(9560, 1100, 11560, 2500, F)
    d.rect(9640, 1200, 10040, 2400, F)
    d.line((10500, 1100), (10500, 2500), F)
    d.rect(13250, 800, 13850, 2300, F)
    d.circle((12950, 1550), 250, F)
    # bath
    d.rect(5160, 8100, 6860, 8850, F)
    d.rect(5260, 8190, 6760, 8760, F)
    d.circle((6560, 8475), 50, F)
    d.rect(7240, 8600, 7680, 8850, F)
    d.ellipse((7460, 8300), (0, -300), 0.7, F)
    d.rect(5060, 6200, 5560, 7200, F)
    d.ellipse((5310, 6700), (0, 300), 0.7, F)
    tiles = [[(5060, 5260), (7740, 5260), (7740, 8850), (5060, 8850)]]
    d.hatch(tiles, "NET", "A-FLOR-PATT", scale=2400)
    # study
    d.rect(7960, 8250, 9400, 8850, F)
    d.circle((8680, 7900), 280, F)
    d.rect(10640, 5500, 10940, 8500, F)
    for y in range(6100, 8500, 600):
        d.line((10640, y), (10940, y), F)
    # utility
    for x0 in (11160, 11820):
        d.rect(x0, 8250, x0 + 600, 8850, F)
        d.circle((x0 + 300, 8550), 220, F)
    d.rect(13000, 8350, 13750, 8850, F)
    d.rect(13080, 8420, 13670, 8790, F)
    d.hatch([[(11060, 5260), (13850, 5260), (13850, 8850), (11060, 8850)]], "NET", "A-FLOR-PATT", scale=2400)

    # Rooms: names and areas.
    rooms = [
        ("LIVING", (0, 0, 5000, 5200)), ("KITCHEN / DINING", (0, 5200, 5000, H)),
        ("BEDROOM 1", (5000, 0, 9500, 4000)), ("BEDROOM 2", (9500, 0, W, 4000)),
        ("BATH", (5000, 5200, 7800, H)), ("STUDY", (7800, 5200, 11000, H)),
        ("UTILITY", (11000, 5200, W, H)), ("HALL", (5000, 4000, W, 5200)),
    ]

    def inner(x0, y0, x1, y1):
        def half(v, lo, hi):
            return ext_t / 2 if v in (lo, hi) else int_t / 2
        w = (x1 - x0) - half(x0, 0, W) - half(x1, 0, W)
        h = (y1 - y0) - half(y0, 0, H) - half(y1, 0, H)
        return w * h / 1e6

    label_at = {"LIVING": (2500, 4550), "KITCHEN / DINING": (2500, 7950), "BEDROOM 1": (7250, 3450),
                "BEDROOM 2": (11750, 3450), "BATH": (6450, 7500), "STUDY": (9400, 7000),
                "UTILITY": (12500, 7300), "HALL": (9500, 4500)}
    for name, box in rooms:
        x, y = label_at[name]
        d.text((x, y), 300, name, "A-ANNO-TEXT", just=1)
        d.text((x, y - 400), 220, f"{inner(*box):.1f} m2", "A-ANNO-TEXT", just=1)

    # Grid.
    cols = [("A", 0), ("B", 5000), ("C", 9500), ("D", W)]
    rows = [("1", 0), ("2", 4600), ("3", H)]
    for label, x in cols:
        d.line((x, -700), (x, H + 1150), "A-GRID")
        bubble(d, "A-GRID-IDEN", "A-GRID-IDEN", (x, H + 1600), 450, label, 380)
    for label, y in rows:
        d.line((-2650, y), (W + 700, y), "A-GRID")
        bubble(d, "A-GRID-IDEN", "A-GRID-IDEN", (-3100, y), 450, label, 380)

    # Dimensions.
    for (a, b) in ((0, 5000), (5000, 9500), (9500, W)):
        d.dim(0, (a, 0), (b, 0), ((a + b) / 2, -1100), "A-ANNO-DIMS", "ARCH")
    d.dim(0, (0, 0), (W, 0), (W / 2, -1900), "A-ANNO-DIMS", "ARCH")
    for (a, b) in ((0, 4600), (4600, H)):
        d.dim(0, (0, a), (0, b), (-1100, (a + b) / 2), "A-ANNO-DIMS", "ARCH")
    d.dim(0, (0, 0), (0, H), (-1900, H / 2), "A-ANNO-DIMS", "ARCH")

    # North point.
    nc = (W + 1900, H - 300)
    d.circle(nc, 550, "A-ANNO-TEXT")
    d.poly([(nc[0], nc[1] + 700), (nc[0] - 260, nc[1] - 400), (nc[0], nc[1] - 150),
            (nc[0] + 260, nc[1] - 400)], "A-ANNO-TEXT", closed=True)
    d.hatch([[(nc[0], nc[1] + 700), (nc[0] - 260, nc[1] - 400), (nc[0], nc[1] - 150)]], "SOLID", "A-ANNO-TEXT")
    d.text((nc[0], nc[1] + 950), 320, "N", "A-ANNO-TEXT", just=1)

    d.text((0, -3000), 560, "GROUND FLOOR PLAN", "A-ANNO-TEXT")
    d.text((0, -3500), 260, "SCALE 1:100", "A-ANNO-TEXT")

    # South elevation, to the right of the plan.
    dx, G = 25500, 3900
    FFL, EAVES, RIDGE = G + 450, G + 450 + 2900, G + 450 + 5000
    x0, x1 = dx - 150, dx + W + 150
    d.hatch([[(dx - 1500, G - 350), (dx + W + 1500, G - 350), (dx + W + 1500, G), (dx - 1500, G)]],
            "EARTH", "A-SITE", scale=700)
    d.hatch([[(x0, G), (x1, G), (x1, FFL), (x0, FFL)]], "CONC", "A-ELEV-PATT", scale=900)
    d.rect(x0, G, x1, FFL, "A-ELEV")
    glazing = []
    wall_loops = [[(x0, FFL), (x1, FFL), (x1, EAVES), (x0, EAVES)]]
    for a, b in windows[0]:
        wx0, wx1 = dx + a, dx + b
        sill, head = FFL + 900, FFL + 2350
        wall_loops.append([(wx0, sill - 60), (wx1 + 60, sill - 60), (wx1 + 60, head), (wx0 - 60, head),
                           (wx0 - 60, sill - 60)])
        d.rect(wx0, sill, wx1, head, "A-ELEV")
        d.rect(wx0 - 60, sill - 60, wx1 + 60, sill, "A-ELEV")
        mid = (wx0 + wx1) / 2
        for gx0, gx1 in ((wx0 + 70, mid - 35), (mid + 35, wx1 - 70)):
            glazing.append([(gx0, sill + 70), (gx1, sill + 70), (gx1, head - 70), (gx0, head - 70)])
            d.rect(gx0, sill + 70, gx1, head - 70, "A-ELEV")
    d.hatch(wall_loops, "BRICK", "A-ELEV-PATT", scale=700)
    d.hatch(glazing, "SOLID", "A-ELEV-GLAZ")
    d.rect(x0, FFL, x1, EAVES, "A-ELEV")
    roof = [(dx - 650, EAVES), (dx + W + 650, EAVES), (dx + W - 2000, RIDGE), (dx + 2000, RIDGE)]
    d.hatch([roof], "LINE", "A-ELEV-ROOF", scale=1600)
    d.poly(roof, "A-ELEV", closed=True)
    d.poly([(dx - 700, EAVES - 40), (dx + W + 700, EAVES - 40)], "A-ELEV", width=90)
    d.poly([(dx - 1500, G), (dx + W + 1500, G)], "A-ELEV", width=60)
    for label, y in (("GL  +0.000", G), ("FFL  +0.450", FFL), ("EAVES  +3.350", EAVES),
                     ("RIDGE  +5.450", RIDGE)):
        level_mark(d, "A-ANNO-TEXT", dx + W + 2100, y, label, 260)
    d.text((dx, G - 1000), 560, "SOUTH ELEVATION", "A-ANNO-TEXT")
    d.text((dx, G - 1500), 260, "SCALE 1:100", "A-ANNO-TEXT")

    # Room schedule, under the elevation.
    tx, ty, row = dx, 1200, 540
    colw = [1100, 4200, 2000, 4300]
    heads = ["No.", "ROOM", "AREA m2", "FLOOR FINISH"]
    finishes = {"LIVING": "OAK BOARDS", "KITCHEN / DINING": "PORCELAIN TILE", "BEDROOM 1": "OAK BOARDS",
                "BEDROOM 2": "OAK BOARDS", "BATH": "CERAMIC TILE", "STUDY": "OAK BOARDS",
                "UTILITY": "CERAMIC TILE", "HALL": "PORCELAIN TILE"}
    body = [(f"{i + 1:02d}", name, f"{inner(*box):.1f}", finishes[name]) for i, (name, box) in enumerate(rooms)]
    total_w = sum(colw)
    nrows = len(body) + 1
    d.rect(tx, ty - nrows * row, tx + total_w, ty, "A-TABL")
    for r in range(1, nrows):
        d.line((tx, ty - r * row), (tx + total_w, ty - r * row), "A-TABL")
    cx = tx
    for w in colw[:-1]:
        cx += w
        d.line((cx, ty), (cx, ty - nrows * row), "A-TABL")
    for r, cells in enumerate([heads] + body):
        cx = tx
        for w, cell in zip(colw, cells):
            d.text((cx + 150, ty - r * row - row + 140), 250, cell, "A-ANNO-TEXT")
            cx += w
    d.text((tx, ty + 250), 360, "ROOM SCHEDULE", "A-ANNO-TEXT")
    d.save(path)


# ---------------------------------------------------------------------------
# 2. The flange: front view, section A-A, dimensions, GD&T (mm, 1:2)
# ---------------------------------------------------------------------------


def flange(path):
    d = Dxf(ltscale=0.9)
    d.layer("OUTLINE", (235, 235, 235), lw=50)
    d.layer("THIN", (200, 200, 200), lw=18)
    d.layer("CENTER", (224, 96, 96), "CENTER", lw=18)
    d.layer("HATCH", (120, 150, 196), lw=13)
    d.layer("DIMS", (100, 206, 170), lw=18)
    d.layer("TEXT", (238, 210, 120), lw=25)
    d.layer("BORDER", (170, 174, 188), lw=35)
    d.dimstyle("ISO-5", 5, 3.5, 0)

    # Front view.
    O = "OUTLINE"
    d.circle((0, 0), 100, O)
    d.circle((0, 0), 55, O)
    d.circle((0, 0), 25, O)
    d.poly([(-7, 24.0), (-7, 28.8), (7, 28.8), (7, 24.0)], O)
    d.circle((0, 0), 75, "CENTER")
    holes = [(75 * math.cos(math.radians(a)), 75 * math.sin(math.radians(a))) for a in range(30, 390, 60)]
    for hx, hy in holes:
        d.circle((hx, hy), 9, O)
        d.line((hx - 13, hy), (hx + 13, hy), "CENTER")
        d.line((hx, hy - 13), (hx, hy + 13), "CENTER")
    d.line((-116, 0), (116, 0), "CENTER")
    d.line((0, -116), (0, 116), "CENTER")
    # Cutting plane A-A.
    for y in (122, -122):
        d.poly([(0, y), (0, y - 12 if y > 0 else y + 12)], O, width=1.2)
        d.leader((14, y), (0, y), "THIN", "ISO-5")
        d.text((18, y - 3), 7, "A", "TEXT")
    d.text((0, -140), 6, "VIEW FROM FRONT", "TEXT", just=1)

    # Section A-A, the axis along x.
    sx = 150

    def region(pts, mirror=False):
        if mirror:
            pts = [(x, -y) for x, y in pts]
        pts = [(sx + x, y) for x, y in pts]
        d.hatch([pts], "ANSI31", "HATCH", scale=24)
        d.poly(pts, O, closed=True)

    top = [(0, 84), (20, 84), (20, 100), (0, 100)]
    body_top = [(-3, 28.8), (-3, 55), (0, 55), (0, 66), (20, 66), (20, 45), (56, 45), (60, 41), (60, 28.8)]
    body_bottom = [(-3, 25), (-3, 55), (0, 55), (0, 66), (20, 66), (20, 45), (56, 45), (60, 41), (60, 25)]
    region(top)
    region(top, mirror=True)
    region(body_top)
    region(body_bottom, mirror=True)
    for y in (75, -75):
        d.line((sx - 8, y), (sx + 28, y), "CENTER")
    d.line((sx - 12, 0), (sx + 72, 0), "CENTER")
    d.text((sx + 30, -140), 6, "SECTION A-A", "TEXT", just=1)

    # Dimensions.
    D = "DIMS"
    q = math.sqrt(0.5)
    d.dim(3, (0, 0), (100 * q, 100 * q), (100 * q, 100 * q), D, "ISO-5")
    d.dim(3, (0, 0), (-25 * q, -25 * q), (-25 * q, -25 * q), D, "ISO-5")
    d.dim(5, (0, 0), holes[0], holes[5], D, "ISO-5")
    d.dim(0, (sx, 100), (sx + 20, 100), (sx + 10, 114), D, "ISO-5")
    d.dim(0, (sx - 3, -55), (sx + 60, -41), (sx + 28, -118), D, "ISO-5")
    d.dim(0, (sx + 60, 45), (sx + 60, -45), (sx + 78, 0), D, "ISO-5")
    d.dim(0, (sx + 20, 100), (sx + 20, -100), (sx + 96, 0), D, "ISO-5")
    d.leader(holes[1], (-70, 118), "THIN", "ISO-5")
    d.text((-128, 121), 5, "6X %%c18 THRU", "TEXT")
    d.text((-128, 113), 5, "EQ SP ON %%c150 PCD", "TEXT")
    d.tolerance((-128, 100), ["{\\Fgdt;j}", "{\\Fgdt;n}0.2{\\Fgdt;m}", "A", "B"], "DIMS", "ISO-5")
    d.leader((sx + 58, 43), (sx + 62, 126), "THIN", "ISO-5")
    d.tolerance((sx + 64, 126), ["{\\Fgdt;b}", "0.05", "A"], "DIMS", "ISO-5")

    # Notes, border and title block.
    d.mtext((262, 118), 5, ["NOTES:", "1. DIMENSIONS IN MILLIMETRES.", "2. BREAK SHARP EDGES 0.5 X 45%%d.",
                            "3. GENERAL TOLERANCES ISO 2768-m.", "4. FACE A FLAT WITHIN 0.05."], "TEXT", width=160)
    d.rect(-150, -152, 430, 148, "BORDER")
    d.rect(262, -152, 430, -82, "BORDER")
    for y in (-100, -118, -136):
        d.line((262, y), (430, y), "BORDER")
    d.line((346, -152), (346, -100), "BORDER")
    d.text((270, -95), 8, "WELD-NECK FLANGE", "TEXT")
    d.text((270, -111), 5, "DWG No. MC-1024", "TEXT")
    d.text((354, -111), 5, "SHEET 1 OF 1", "TEXT")
    d.text((270, -129), 5, "MATERIAL S355J2", "TEXT")
    d.text((354, -129), 5, "SCALE 1:2", "TEXT")
    d.text((270, -147), 5, "REV B", "TEXT")
    d.text((354, -147), 5, "A3", "TEXT")
    d.save(path)


# ---------------------------------------------------------------------------
# 3. The wall / strip-footing detail (mm, 1:10)
# ---------------------------------------------------------------------------


def footing_detail(path):
    d = Dxf(ltscale=6)
    d.layer("S-CONC", (170, 170, 176), lw=35)
    d.layer("S-CONC-PATT", (128, 132, 140), lw=9)
    d.layer("A-BRCK", (204, 120, 92), lw=35)
    d.layer("A-BRCK-PATT", (170, 96, 72), lw=9)
    d.layer("A-BLCK", (190, 190, 196), lw=35)
    d.layer("A-BLCK-PATT", (118, 122, 132), lw=9)
    d.layer("A-INSL", (232, 206, 96), lw=18)
    d.layer("A-MEMB", (96, 150, 232), lw=18)
    d.layer("G-SOIL", (138, 110, 78), lw=9)
    d.layer("G-FILL", (150, 142, 124), lw=9)
    d.layer("A-ANNO-TEXT", (238, 210, 120), lw=18)
    d.layer("A-ANNO-DIMS", (100, 206, 190), lw=13)
    d.layer("A-TTLB", (170, 174, 188), lw=35)
    d.dimstyle("DET-10", 32, 24, 0)

    def region(pts, pattern, patt_layer, scale, edge_layer, angle=0.0):
        d.hatch([pts], pattern, patt_layer, scale=scale, angle=angle)
        d.poly(pts, edge_layer, closed=True)

    TOP = 700
    # Ground.
    region([(-1000, 0), (-290, 0), (-290, -650), (-350, -650), (-350, -950), (-1000, -950)],
           "EARTH", "G-SOIL", 400, "G-SOIL")
    region([(10, -400), (1000, -400), (1000, -950), (450, -950), (450, -650), (10, -650)],
           "EARTH", "G-SOIL", 400, "G-SOIL")
    # Footing.
    region([(-350, -950), (450, -950), (450, -650), (-350, -650)], "CONC", "S-CONC-PATT", 200, "S-CONC")
    # Leaves.
    region([(-290, -650), (-190, -650), (-190, TOP), (-290, TOP)], "BRICK", "A-BRCK-PATT", 300, "A-BRCK")
    region([(-90, -650), (10, -650), (10, TOP), (-90, TOP)], "ANSI32", "A-BLCK-PATT", 160, "A-BLCK")
    # Cavity: lean-mix fill below ground, insulation above the DPC.
    region([(-190, -650), (-90, -650), (-90, -150), (-190, -150)], "CONC", "S-CONC-PATT", 140, "S-CONC")
    region([(-165, 150), (-90, 150), (-90, TOP), (-165, TOP)], "INSUL", "A-INSL", 70, "A-INSL")
    # Floor build-up.
    region([(10, -400), (1000, -400), (1000, -200), (10, -200)], "GRAVEL", "G-FILL", 200, "G-FILL")
    region([(10, -200), (1000, -200), (1000, -100), (10, -100)], "INSUL", "A-INSL", 90, "A-INSL")
    region([(10, -100), (1000, -100), (1000, 50), (10, 50)], "CONC", "S-CONC-PATT", 160, "S-CONC")
    region([(10, 50), (1000, 50), (1000, 115), (10, 115)], "DOTS", "S-CONC-PATT", 160, "S-CONC")
    # Membranes: the DPM under the slab turned up the wall, the DPCs in both leaves.
    d.poly([(1000, -100), (18, -100), (18, 150)], "A-MEMB", width=8)
    d.poly([(-290, 150), (-190, 150)], "A-MEMB", width=10)
    d.poly([(-90, 150), (10, 150)], "A-MEMB", width=10)
    # Ground line and break lines.
    d.poly([(-1000, 0), (-290, 0)], "G-SOIL", width=10)
    for x0, x1 in ((-300, 20),):
        d.poly([(x0, TOP), (x0 + 120, TOP), (x0 + 150, TOP + 50), (x0 + 190, TOP - 50), (x0 + 220, TOP),
                (x1, TOP)], "A-TTLB")
    d.poly([(1000, 160), (1000, 60), (1050, 20), (950, -40), (1000, -80), (1000, -420)], "A-TTLB")

    # Labels.
    T, L = "A-ANNO-TEXT", "A-ANNO-TEXT"
    left = [((-240, 480), "102.5 FACING BRICK"), ((-128, 420), "75 PIR CAVITY INSULATION"),
            ((-240, 150), "DPC 150 ABOVE GROUND"), ((-140, -400), "LEAN-MIX CAVITY FILL")]
    for i, (tip, label) in enumerate(left):
        ky = 660 - i * 150
        d.leader(tip, (-620, ky), L, "DET-10")
        d.text((-640, ky - 12), 34, label, T, just=2)
    right = [((-40, 560), "100 DENSE CONCRETE BLOCK"), ((500, 82), "65 SAND / CEMENT SCREED"),
             ((500, -30), "150 CONCRETE SLAB ON 1200g DPM"), ((600, -150), "100 FLOOR INSULATION"),
             ((700, -300), "200 COMPACTED HARDCORE"), ((200, -800), "800 x 300 C25 STRIP FOOTING")]
    for i, (tip, label) in enumerate(right):
        ky = 600 - i * 230
        d.leader(tip, (1120, ky), L, "DET-10")
        d.text((1140, ky - 12), 34, label, T)

    # Dimensions and levels.
    d.dim(0, (-350, -950), (450, -950), (50, -1060), "A-ANNO-DIMS", "DET-10")
    d.dim(0, (-350, -650), (-350, -950), (-470, -800), "A-ANNO-DIMS", "DET-10")
    d.dim(0, (-1000, 0), (-1000, -950), (-1120, -475), "A-ANNO-DIMS", "DET-10")
    level_mark(d, T, -800, 0, "GL  +0.000", 36)
    level_mark(d, T, 700, 115, "FFL  +0.115", 36)

    # Border and title block.
    d.rect(-1400, -1250, 2200, 820, "A-TTLB")
    d.rect(1300, -1250, 2200, -900, "A-TTLB")
    d.line((1300, -1030), (2200, -1030), "A-TTLB")
    d.line((1300, -1140), (2200, -1140), "A-TTLB")
    d.line((1750, -1250), (1750, -1030), "A-TTLB")
    d.text((1330, -985), 52, "DETAIL 04", T)
    d.text((1330, -1010), 22, "EXTERNAL WALL / STRIP FOOTING", T)
    d.text((1330, -1095), 26, "SHEET A-501", T)
    d.text((1780, -1095), 26, "SCALE 1:10", T)
    d.text((1330, -1205), 26, "REV C", T)
    d.text((1780, -1205), 26, "A3", T)
    d.save(path)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    residence(os.path.join(out, "residence.dxf"))
    flange(os.path.join(out, "flange.dxf"))
    footing_detail(os.path.join(out, "footing-detail.dxf"))
    print(f"drawings written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
