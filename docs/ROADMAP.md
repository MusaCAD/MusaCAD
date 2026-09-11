<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Musa CAD — implementation roadmap

A survey of what is **not yet implemented**, grouped by theme, with the AutoCAD behaviour
each item is measured against. `docs/COMMANDS.md` is the per-command status table and
`docs/TODO.md` holds the fine-grained deferrals with rationale; this file is the map above
both, and each group below corresponds to a GitHub issue.

Status legend: **P0** blocks ordinary drafting · **P1** expected by a professional user ·
**P2** completeness / parity.

---

## A. Dimension editing parity — [#20](https://github.com/MusaCAD/MusaCAD/issues/20), [#21](https://github.com/MusaCAD/MusaCAD/issues/21), [#28](https://github.com/MusaCAD/MusaCAD/issues/28)

The dimension model is strong (value computed from def points, never baked, one shared
`compute_dim_geometry`). What is missing is the *editing* surface AutoCAD gives.

| Item | AutoCAD behaviour | Priority |
|---|---|---|
| ~~**Text override**~~ | **Done** (#20) — `<>` expands to the measurement. | ✅ |
| ~~**Text reposition grip**~~ | **Done** (#21) — text grip on all five types, connector leader, "home text". | ✅ |
| ~~**DIMBASELINE / DIMCONTINUE**~~ | **Done** (#28) — DBA / DCO chain from the last dimension's extension line. | ✅ |
| ~~**DIMJOGGED / DIMARC / DIMORDINATE**~~ | **Done** (#28) — DJO / DAR / DOR, native and DXF. | ✅ |
| **Narrow-dimension fit for radial/angular** | The ISO 129-1 fallback covers linear/aligned only. | **P2** |
| **Short-leader variant for outside text** | ISO 129-1 permits the value on a short leader. | **P2** |

## B. Tables and structured annotation — [#22](https://github.com/MusaCAD/MusaCAD/issues/22), [#29](https://github.com/MusaCAD/MusaCAD/issues/29)

| Item | AutoCAD behaviour | Priority |
|---|---|---|
| ~~**TABLE entity + TABLESTYLE**~~ | **Done** (#22) — merged cells, per-cell alignment, style-driven text heights. Cell editing staged. | ✅ |
| ~~**Text STYLE table**~~ | **Done** (#29) — STYLE / -STYLE, a current style, PR style picker, native + DXF STYLE table. | ✅ |
| **FIELD** | ~~Date, time, filename, login~~ **Done** (#33), refreshed on regen. Still open: sheet number and object-property fields. | **P2** |

## C. Missing draw primitives — [#23](https://github.com/MusaCAD/MusaCAD/issues/23), [#33](https://github.com/MusaCAD/MusaCAD/issues/33)

The store already has arenas for splines and points; the *commands* are missing.

| Item | Priority |
|---|---|
| ~~**SPLINE**, **ELLIPSE**, **POLYGON**, **POINT**, **XLINE / RAY** commands~~ | **Done** (#23) ✅ |
| ~~**DONUT**, **REVCLOUD**, **WIPEOUT**~~ | **Done** (#23, #33) ✅ |
| ~~RECTANGLE first-corner options (Chamfer / Fillet / Width)~~ | **Done** (#23) ✅ |
| ~~HATCH gradient fills~~ | **Done** (#33) — two colours along an angle, DXF gradient block both ways. ✅ |

## D. Missing modify commands — [#24](https://github.com/MusaCAD/MusaCAD/issues/24), [#27](https://github.com/MusaCAD/MusaCAD/issues/27)

| Item | Priority |
|---|---|
| ~~**STRETCH**~~ | **Done** (#24) | ✅ |
| ~~**BREAK**, **LENGTHEN**, **ALIGN**~~ | **Done** (#27) ✅ |
| ~~**DIVIDE / MEASURE**~~ | **Done** (#27) — points at intervals; the Block option is still open. ✅ |
| ~~**PEDIT**~~ | **Done** (#27) — Close/Open, Join, Edit vertex (Insert/Delete/Move), Spline, Decurve, Reverse, Undo. ✅ |
| ~~TRIM / EXTEND / FILLET on **curve entities**~~ | **Done** (#27) ✅ |

## E. Block authoring — [#25](https://github.com/MusaCAD/MusaCAD/issues/25)

Blocks can be imported and placed; they cannot be *created* in-app.

| Item | Priority |
|---|---|
| ~~**BLOCK / WBLOCK**~~ — define a block from a selection | **Done** (#25) ✅ |
| ~~**EXPLODE**~~ — instance → geometry | **Done** (#25) ✅ |
| ~~**REFEDIT**~~ — edit a definition in place | **Done** (#25) — REFEDIT / REFSET / REFCLOSE. ✅ |
| ~~**ATTDEF / ATTRIB**~~ — block attribute text | **Done** (#25) — ATTDEF, INSERT prompts, ATTDISP, ATTEDIT, DXF ATTRIB. ✅ |
| ~~**XREF**~~ — external references | **Done** (#25) — attach, detach, reload, re-read on open. ✅ |

## F. Raster images — [#10](https://github.com/MusaCAD/MusaCAD/issues/10) (remainder)

Done: model, decoder seam, persistence, plot, viewport display (a textured-quad pipeline with a
per-definition texture cache), IMAGEATTACH / IMAGECLIP (rectangular and polygonal) / IMAGEFRAME,
the 8 MB embed limit, and DXF `IMAGE`/`IMAGEDEF` both ways (the writer now gives every record a
handle and carries CLASSES and the image dictionary).

## G. Paper space, layouts and views — [#26](https://github.com/MusaCAD/MusaCAD/issues/26), [#33](https://github.com/MusaCAD/MusaCAD/issues/33)

| Item | Priority |
|---|---|
| ~~**Layouts / paper space + viewports**~~ | **Done** (#26) — layouts, paper-space entities, tabs, sheet plot, MVIEW viewports, MSPACE / PSPACE through a viewport. ✅ |
| ~~**Named views (VIEW)**, **REGEN**~~ **Done** (#33); **VPORTS** | **P2** |

## H. Inquiry and drawing housekeeping — [#30](https://github.com/MusaCAD/MusaCAD/issues/30)

| Item | Priority |
|---|---|
| ~~**DIST / AREA / ID / LIST**~~ | **Done** (#30) ✅ |
| ~~**PURGE / AUDIT**~~ | **Done** (#30) ✅ |
| ~~**UNITS** (drawing units + precision)~~ | **Done** (#30) ✅ |
| ~~**GROUP**~~ | **Done** (#33) — GROUP / UNGROUP / PICKSTYLE. ✅ |

## I. Interop gaps — [#31](https://github.com/MusaCAD/MusaCAD/issues/31)

(All stated honestly in the import result string today, never silently dropped.)

| Item | Priority |
|---|---|
| ~~DXF **SPLINE** / legacy **POLYLINE** import~~ | **Done** (#31) ✅ |
| ~~DXF **TOLERANCE** (GD&T) export/import~~ | **Done** (#31) ✅ |
| ~~DXF **IMAGE / IMAGEDEF**~~ | **Done** (#31) — with object handles, CLASSES and the image dictionary. ✅ |
| True **SHX** shape-file parsing (today: faithful substitution) | **P2** |

## J. Properties, snapping and input — [#32](https://github.com/MusaCAD/MusaCAD/issues/32)

| Item | Priority |
|---|---|
| ~~PR **numeric geometry editing**~~ | **Done** (#32) — Start/End, Center/Radius and Position fields edit the entity. ✅ |
| ~~OSNAP settings dialog; **apparent intersection / insertion / parallel** snaps~~ | **Done** (#32) ✅ |
| ~~Input dialogs for Rotate/Scale + live ghost preview~~ | **Done** (#32) ✅ |

---

## Ordering rationale

**P0 first, and within P0 the dimension-editing pair leads**, because a drafter hits
"I need this dimension to read `<> H7`" and "I need to move this text off the geometry"
on essentially every real sheet, and neither has a workaround that keeps the dimension
trustworthy. Tables come next (every fabrication sheet has a BOM or revision block), then
block authoring and paper space, which are larger structural pieces.
