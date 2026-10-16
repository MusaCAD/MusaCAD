# Musa CAD — Commands & Shortcuts (living roadmap)

This is the living specification of Musa CAD's AutoCAD-style commands. Each row
has a **Status**: `Implemented`, or `Planned (Phase N)`. Every future phase
implements a slice and flips rows to `Implemented`. Phase numbers beyond the
current one are a tentative roadmap and may shift.

Conventions: type the **Alias** or full name at the command line (autocomplete
suggests both); aliases are case-insensitive.

## Command descriptions (ribbon tooltips)

Every command carries a one-line **description** and an **icon** in its registry row
(`CommandRegistry::make_default`, the single source of truth). The ribbon reads these to
build each button's icon and its hover tooltip (`NAME (ALIAS) — description`); the same
descriptions are available for future help / command-search. Catalog of the 38 registered
commands (Ribbon Phase A):

| Command (alias) | Description |
|---|---|
| LINE (L) | Create a series of straight-line segments. |
| CIRCLE (C) | Draw a circle by centre and radius or diameter, two or three points, or tangent to objects. |
| PLINE (PL) | Draw a connected sequence of line and arc segments as one object. |
| ARC (A) | Draw an arc by three points, or from a start, centre or end with an angle, chord, direction or radius. |
| RECTANGLE (REC) | Draw a rectangle from two opposite corners. |
| ERASE (E) | Delete objects: `Select objects:` gathers picks, windows and keywords until Enter (a pre-selection is erased at once); OOPS brings the set back. |
| OOPS | Bring back the objects the last ERASE removed, keeping everything done since. |
| SELECT | Select objects with AutoCAD's keywords (Window, Crossing, BOX, ALL, Fence, WPolygon, CPolygon, Group, Last, Previous, Add, Remove, Undo, SIngle); the set stays for the next command. |
| SELECTSIMILAR | Select every object of the same kind and properties as the selected ones (SELECTSIMILARMODE says which properties count; default layer and name). |
| QSELECT / FILTER (FI) | The Quick Select dialog: by object type and one property (FILTER: several conditions, all of which must hold). |
| MEASUREGEOM (MEA) | Measure Distance, Radius, Angle, ARea or Volume, repeating until eXit; Quick mode measures the object clicked. |
| MASSPROP | The area, perimeter, bounding box, centroid, moments and product of inertia, radii of gyration and principal moments of the selected closed shapes. |
| TIME | The current time, the drawing's creation and last-saved times, its total editing time and the elapsed timer (Display/ON/OFF/Reset). |
| STATUS | The object count, the space's extents, the drafting modes and the current layer, colour, linetype, lineweight and units. |
| CAL | One expression: arithmetic, sin/cos/tan (degrees), sqrt, ln, log, [x,y] points, dist, ang, vec, nor, abs, cvunit(v, from, to). |
| QUICKCALC (QC) | The calculator palette: a number pad, scientific functions, unit conversion, history, paste to the command line. |
| DWGPROPS | The drawing's properties: General, Summary (title, subject, author, keywords, comments, hyperlink base), Statistics and Custom fields, saved with the drawing. |
| ISOLATEOBJECTS / HIDEOBJECTS / UNISOLATEOBJECTS | Hide everything but the selection, hide the selection, show everything again. Hidden objects are neither drawn nor pickable; the flag is saved with the drawing. |
| PICKBOX, PICKFIRST, PICKADD, PICKAUTO, PICKDRAG, HIGHLIGHT, SELECTIONPREVIEW, SELECTIONCYCLING | The selection system variables, `Enter new value for X <current>:`. |
| GRIPS, GRIPSIZE, GRIPCOLOR, GRIPHOT | The grip variables: grips shown (1) or not (0), their size in pixels from centre to edge, and their colour and the hot grip's as AutoCAD colour numbers. |
| UNDO (U) | Reverse the most recent action. |
| HELP (?, F1) | Open this command reference in the browser. |
| PREVIEW (PRE) | The plot as it will come out, with the last plot's settings: zoom, pan by dragging, Plot. |
| EXPORTPDF | The layout in front (its sheet, 1:1) or the model (the last plot's settings) saved as a PDF file. Output > Export > PDF. |
| NAVBAR | `Enter an option [ON/OFF] <ON>:` -- the navigation bar at the drawing's right edge: Pan, Zoom Extents, Zoom Window, Zoom Previous, Zoom In and Zoom Out. View > Viewport Tools > Navigation Bar. |
| UPDATEFIELD | `Select objects:`; the fields in their text, attributes and table cells counted and worked out again (`2 field(s) found. 2 field(s) updated.`). Insert > Data > Update Fields. |
| PAGESETUP | The Page Setup Manager: the drawing's page setups; New... names one and sets it in the Plot window (OK in place of Plot), Modify... changes one, Delete, and Set Current makes it what PLOT starts from in model space. Output > Plot > Page Setup Manager. |
| PUBLISH | The Publish window: the model and every layout, ticked to publish; Publish... asks for a file and plots each sheet ticked as a page of one PDF, each page its own paper size. Output > Plot > Batch Plot. |
| ZOOM (Z) | Zoom in or out to change the view magnification. |
| PAN (P) / -PAN (-P) | Move the view with a drag, or by a displacement. |
| MOVE (M) | Move selected objects a specified distance and direction. |
| COPY (CO) | Duplicate selected objects at a specified offset. |
| MIRROR (MI) | Create a mirror-image copy of objects across an axis. |
| OFFSET (O) | Create a parallel copy of a curve at a specified distance: lines, arcs, circles, polylines, construction lines and rays; ellipses and splines give a spline. The offset follows the cursor at the side prompt. |
| OFFSETGAPTYPE | What joins a polyline's straight segments where an offset has parted them: 0 carried on to their crossing, 1 an arc, 2 a bevel. |
| TRIM (TR) | Trim objects to meet the edges of other objects: pick the part to remove, or drag a path across many and each is trimmed where the path crosses it. Shift extends instead. |
| JOIN (J) | Join collinear or connected objects into a single object. |
| ROTATE (RO) | Rotate selected objects around a base point. |
| SCALE (SC) | Resize selected objects uniformly about a base point. |
| ARRAY (AR), -ARRAY | Create a rectangular, path or polar pattern of copies. |
| ARRAYRECT | Create a rectangular pattern of copies in rows and columns. |
| ARRAYPOLAR | Create a circular pattern of copies about a centre point. |
| ARRAYPATH | Distribute copies evenly along a path curve. |
| POLYGON (POL) | Draw a regular polygon by centre or by one edge. |
| REVCLOUD | Draw a revision cloud, or turn an object into one. |
| EXPLODE (X) | Break compound objects into their components. |
| PURGE (PU) | The Purge dialog: the unused blocks, dimension styles, groups, layers, table styles, images and text styles by name, checked off; Confirm each item; zero-length geometry and empty text objects. |
| -PURGE | Purge from the command line: the type, `Enter name(s) to purge <*>:` (wild cards), `Verify each name to be purged? [Yes/No] <Y>:` and one question per name. |
| POLARANG / POLARADDANG / POLARMODE / POLARDIST / SNAPTYPE / AUTOSNAP / TEMPOVERRIDES | The tracking settings as system variables: the polar increment angle, the additional angles (`15;75`, `.` for none), relative angles (1) / tracking along every polar angle (2) / additional angles (4), the PolarSnap distance, grid snap (0) or PolarSnap (1), polar (8) and object snap (16) tracking, the temporary override keys. |
| DSETTINGS (DS, SE, RM, DDRMODES), OSNAP (OS, DDOSNAP) / -OSNAP / OSMODE | The Drafting Settings dialog: **Snap and Grid** (snap on, X / Y spacing, polar distance, Grid snap or PolarSnap; grid on, X / Y spacing -- 0 follows the zoom -- the major line, adaptive and subdivision, beyond the limits), **Polar Tracking** (on, the increment and additional angles, tracking orthogonally or along every polar angle, absolute or relative to the last segment), **Object Snap** (on, tracking on, the running snaps) and **Dynamic Input**; it opens where it was left; -OSNAP takes a mode list (END, MID, CEN, GCEN, NOD, QUA, INT, EXT, INS, PER, TAN, NEA, APP, PAR, NONE, ALL) and offers the modes in force as its default; OSMODE is their bit sum (4133 to begin with: Endpoint, Center, Intersection, Extension). |
| WIPEOUT | Mask polygon from points or a closed polyline ([Polyline], with optional erase); [Frames] ON/OFF shows or hides the boundaries, or Display but not plot shows them on screen only. Hides lines, curves and hatches beneath it; text stays visible. |
| FIELD | Text carrying %<Date>%, %<Time>%, %<Filename>% or %<Login>%, expanded at layout and refreshed on every regen. |
| PEDIT (PE) | Edit a polyline, or several (Multiple): Close/Open, Join, Width (one width for every segment), Edit vertex (an X on the current vertex: Next/Previous/Break/Insert/Move/Regen/Straighten/Tangent/Width), Fit (arcs through the vertices), Spline (a fit spline through the vertices), Decurve, Reverse, Undo; a picked line or arc is turned into a polyline first, if you say so. |
| PELLIPSE | 1 makes ELLIPSE draw a polyline of arcs, 0 a true ellipse. |
| BLOCK (B, -BLOCK) | Make the selection a block definition (name, base point, select objects); the originals are replaced by one insert in place. |
| INSERT (I, -INSERT) | Insert a block by name: insertion point, X/Y scale, rotation (? lists blocks). |
| ATTDEF (ATT, -ATTDEF) | Define a block attribute: modes (Invisible/Constant/Verify/Preset), tag, prompt, default value, then the text placement. Shows its tag until BLOCK folds it into a definition. |
| ATTDISP | Attribute visibility: Normal (each attribute's own Invisible mode), ON or OFF for all. |
| ATTEDIT (-ATTEDIT) | Change one attribute value (by tag, or all) on a block reference. |
| EATTEDIT | Enhanced Attribute Editor: pick a block reference (or double-click one) and edit every attribute value in a dialog -- Tag / Prompt / Value, with a Value field bound to the selected row; OK or Apply writes them as one undo step. Insert tab ▸ Block Definition ▸ Edit Attribute. |
| BATTMAN | Block Attribute Manager: choose a block, then edit its attribute definitions in place (tag, prompt, default, modes as I/C/V/P letters, text height), Move Up / Move Down / Remove, and see how many references the active space holds; with "Apply changes to existing references" every reference's values are carried over by tag (a new attribute takes its default, a removed one drops). Insert tab ▸ Block Definition ▸ Manage Attributes. |
| XREF (XR, -XREF) | External references: Attach a .musa or .dxf as a block named after the file (its own blocks come along as name\|block), placed at a point, scale and rotation; Detach erases its references and definition; Reload re-reads the file; ? lists them. Xrefs are re-read whenever the drawing opens. |
| IMAGEATTACH (IAT) | Attach a raster image: file (or the file dialog), keep a copy inside the drawing or reference it from the drawing's folder, insertion point, scale (1 pixel = 1 unit at 1), rotation. |
| IMAGECLIP (ICL) | Clip an image to a rectangular or polygonal boundary (Close with Enter); Delete, ON, OFF drop or keep the boundary. |
| IMAGEFRAME | Image frames: 0 hidden, 1 shown and plotted, 2 shown on screen only. |
| LAYOUT (LO) | Paper-space sheets: New, Copy (objects included), Delete (an empty one), Rename, Set, ?. The tab strip does the same by click and right-click. |
| MODEL / PSPACE (PS) | Switch to model space / to paper space (the first layout). |
| MVIEW (MV) | A viewport on the layout showing model space: two corners, or Fit (the sheet inside a 10 mm margin), fitted to the whole model; ON / OFF, Scale (paper mm per model unit), Center (the model point at the viewport's centre). Corner grips resize, the centre grip moves. |
| MSPACE (MS) | Edit model space through a viewport: pick one (or double-click inside it). The model fills the window at the viewport's scale; draw and edit as in model space; PSPACE (or a double-click on nothing) returns to the sheet, and the view you left becomes the viewport's view, so ZOOM and PAN inside set its scale and centre. |
| VPORTS (-VPORTS, VIEWPORTS) | Tiled model-space viewports, as AutoCAD's Model Viewports: 2 (Horizontal / Vertical), 3 (Horizontal / Vertical / Above / Below / Left / Right) and 4 split the *current* viewport; SIngle returns to one viewport keeping the current view; Join merges two that share an edge (the dominant's view wins); Save / Restore / Delete / ? manage named configurations, saved in the drawing. The View tab's Viewport Configuration list applies a standard layout to the whole window. Click a viewport to make it current (a bright blue border; the click is not a pick, and a running command carries on there); the wheel zooms the viewport under the cursor; every viewport pans and zooms on its own while showing the same model, so a selection or an edit shows in all of them. |
| VPLAYER | Layer visibility per viewport: Freeze / Thaw layers (typed names, or the layers of objects you pick) in the Current viewport (the one you are in through MSPACE), All viewports, or a Selected one; Reset puts a viewport back to the layers' defaults; Newfrz creates layers frozen in every viewport; Vpvisdflt sets a layer's default for viewports created later; ? lists what each viewport freezes. The Layer Properties Manager shows the same as its "New VP Freeze" column and, while a viewport is current, a "VP Freeze" column. Model space itself and the other viewports are unaffected; the lists are saved natively and as the DXF VIEWPORT's frozen-layer handles (331) and LAYER flag 2. |
| REFEDIT | Edit a block definition in place: the picked reference's members become ordinary objects (the working set) to edit with any command. |
| REFSET | Add objects to, or remove them from, the working set of the reference being edited. |
| REFCLOSE | Save the working set back into the block definition (every reference updates) or discard the changes. |
| WBLOCK (W) | Write a block (base point at the origin) or the whole drawing to a .musa file. |
| REGEN (RE), REGENALL (REA) | Rebuild and redraw the scene. |
| STYLE (ST, -STYLE) | Named text styles: font, fixed height, width factor, obliquing angle; TEXT uses the current style and the palette's Style dropdown changes it. |
| UNITS (UN) | The Drawing Units dialog: length and angle type and precision, clockwise, the insertion scale (INSUNITS), the sample output and the Direction Control dialog. |
| -UNITS | The same from the command line: AutoCAD's numbered format tables, `Enter choice, 1 to 5 <2>:`, the fraction denominator, the angle base and direction, `Do you want angles measured clockwise?`. |
| INSUNITS | The drawing unit blocks and images are scaled to on insertion (0 unitless ... 4 millimeters, 6 meters). |
| CELTSCALE / PSLTSCALE / MSLTSCALE | The linetype scale new objects are created with; whether linetypes seen through layout viewports follow the viewport scale (dashes the same length on the sheet); whether model-space linetypes follow the annotation scale (recorded; there is no annotation scale yet). |
| AUDIT | Validate layer/style/font/block/image references and entity shape; Yes fixes them. |
| VIEW (V) | Save, Restore, Delete and list (?) named views; Window saves a framed view. |
| GROUP (G) | Make the selection a named group (Name/Description; `?` lists the groups); picking a member selects the group. |
| -GROUP (-G) | Group options from the command line: ?, Order, Add, Remove, Explode, REName, Selectable, Create. |
| GROUPEDIT | Pick a member (or Name) then Add objects, Remove objects or REName. Ctrl+H (Ctrl+Shift+A) toggles group selection. |
| UNGROUP | Dissolve a group by pick or by name. |
| PICKSTYLE | 0/1: whether picking a member selects its whole group. |
| ALIGN (AL) | Move, rotate and optionally scale a selection onto two destination points. |
| LENGTHEN (LEN) | Measure or change the length of a line, arc, open polyline or elliptical arc. |
| BREAK (BR) | Break a curve between two points. |
| BREAKATPOINT | Split a curve at one point, leaving no gap. |
| ELLIPSE (EL) | Draw an ellipse or elliptical arc (axis endpoints, Center, Rotation, Arc). |
| SPLINE (SPL) | Draw a spline through fit points (Method Fit, Knots Chord/Square root/Uniform) or by control vertices (Method CV, Degree). Undo, Close. |
| DONUT (DO) | Filled ring or disc from inside/outside diameters (typed, or shown by two points): a closed polyline of two arcs, as wide as the ring. |
| FILL / FILLMODE | Whether wide polylines, solids and hatches are filled (ON / 1) or wide polylines outlined and hatches hidden (OFF / 0). Saved with the drawing. |
| PLINEWID | The width new polylines start with. |
| XLINE (XL) | Draw an infinite construction line. |
| RAY | Draw a semi-infinite construction line. |
| POINT (PO) | Place a point; the ribbon's Multiple Points (MULTIPLE POINT) places one after another. |
| MULTIPLE | Repeat a command until Esc (MULTIPLE POINT places point after point). |
| PDMODE | How points are drawn: 0 a dot, 1 nothing, 2 a plus, 3 a cross, 4 a tick; +32 in a circle, +64 in a square. |
| PDSIZE | The size of point markers: in drawing units, or 0 or less for a size on screen. |
| PTYPE | The point display mode and size (PDMODE and PDSIZE). |
| LIMITS | The drawing's limits, and whether points outside them are refused. |
| DIVIDE (DIV) | Mark a curve into a number of equal segments with points. |
| MEASURE (ME) | Mark a curve at set intervals with points. |
| EXTEND (EX) | Extend objects to meet the edges of other objects; a dragged path extends every object it crosses. Shift trims instead. |
| FILLET (F) | Round corners between two intersecting lines, arcs, or polylines. |
| CHAMFER (CHA) | Bevel corners between two intersecting lines. |
| MATCHPROP (MA) | Copy properties from a source object to one or more target objects. |
| HATCH (H) / -HATCH (-H) | Fill an enclosed area with a pattern or solid color: `Pick internal point or [Select objects/Undo/seTtings]:`; seTtings asks the pattern (`Enter a pattern name or [?/Solid/User defined/Gradient] <ANSI31>:`), its scale and angle, kept for the next hatch; -HATCH asks `Specify internal point or [Properties/Select objects/...]:`. |
| TEXT (DT) | Single-line text, line after line: `[Justify/Style]`, AutoCAD's fifteen justifications (Align and Fit between two points), the height and rotation remembered; an empty line ends it. |
| JUSTIFYTEXT | Change the justification of the selected text without moving it. |
| SCALETEXT | Scale the selected text, each about a point of its own (its insertion point, or a justification's point): a new height, a scale factor (or Reference), or the height of another text. |
| TXT2MTXT | Convert the selected single-line text to one multiline text, top line first. |
| DIMLINEAR (DLI) | A horizontal, vertical or rotated linear dimension: two points or Enter to select an object; `[Mtext/Text/Angle/Horizontal/Vertical/Rotated]`. |
| DIMALIGNED (DAL) | A dimension aligned with two points or a selected object; `[Mtext/Text/Angle]`. |
| DIMRADIUS (DRA) | Create a radius dimension for a circle or arc. |
| DIMDIAMETER (DDI) | Create a diameter dimension for a circle or arc. |
| DIMANGULAR (DAN) | An angular dimension of two lines, an arc, a circle or three points, its arc placed where you click; `[Mtext/Text/Angle/Quadrant]`. |
| DIMORDINATE (DOR) | Ordinate (X or Y datum) dimension with a leader; Xdatum/Ydatum or automatic from the leader direction. |
| DIMJOGGED (DJO) | Jogged radius dimension: arc or circle, centre location override, dimension line location, jog location. |
| DIMARC (DAR) | Arc length dimension of an arc or a polyline arc segment. |
| DIM | Create dimensions one after another, each suited to the object picked; Angular, Baseline, Continue, Ordinate, aliGn, Distribute, Layer. |
| DIMLAYER | The layer new dimensions go on; . for the current layer. |
| QDIM | Dimension the selected geometry at once: a chain, staggered, from a baseline, ordinates, radii or diameters. |
| DIMEDIT (DED) | Put dimensions' text home, replace or rotate it, or slant their extension lines (Oblique). |
| DIMTEDIT (DIMTED) | Move or rotate a dimension's text, or slide it Left, Right or to the Center. |
| DIMSPACE | Space parallel dimensions evenly from a base dimension, or line them up with it. |
| DIMJOGLINE | Add a jog to a linear or aligned dimension's line, or remove it. |
| DIMINSPECT | Make dimensions inspection dimensions: a frame, a label and an inspection rate. |
| DIMCENTER (DCE) | Mark the centre of an arc or circle, with centre lines if asked. |
| DIMOVERRIDE (DOV) | Give dimensions settings of their own (DIMTXT, DIMASZ ...), or clear theirs. |
| LAYER (LA) | Open the Layer Properties Manager. |
| -LAYER (-LA) | Layers at the command line: make, set, add, rename, and switch them on, off, frozen, locked. |
| ORTHO | Turn orthogonal drawing on or off (F8). |
| SNAP (SN) | Set the snap spacing, or turn grid snap on or off (F9). |
| GRID | Show or hide the grid (F7). |
| REVERSE | Make lines, polylines and splines run the other way. |
| OVERKILL (-OVERKILL) | Delete duplicate objects and combine overlapping collinear lines. |
| COPYTOLAYER | Copy objects to another layer, in place or moved. |
| LAYMCH | Put objects on the layer of another object, or a layer named. |
| CHPROP | Change objects' colour, layer, linetype, linetype scale or lineweight at the command line. |
| BLEND | A spline joining the ends of two objects, tangent (or smooth) to both. |
| REDO | Redo the last step undone. |
| MREDO | Redo several undone steps, or all of them. |
| TRACE | Draw wide line segments (a polyline with a width). |
| SKETCH | Draw freehand: each stroke becomes lines, a polyline or a spline. |
| BREAKLINE | Draw a break line with a zigzag break symbol. |
| FIND | Find text in the drawing and replace it. |
| TCASE | Change the case of text. |
| SOLID (SO) | Draw filled triangles and quadrilaterals. |
| CENTERMARK (CM) | Mark the centres of circles and arcs, with centre lines. |
| CENTERLINE (CL) | Draw the centre line between two lines. |
| BOUNDARY (BO, -BOUNDARY) | Closed polylines round the area enclosing a point, and its islands. |
| DIMSTYLE (D, DST, DDIM) | The Dimension Style Manager: the drawing's dimension styles, Set Current, New and Modify (lines, arrows, text, primary units). |
| -DIMSTYLE | Dimension styles at the command line: `[Save/Restore/STatus/?]`. |
| DIMTXT, DIMASZ, DIMBLK, DIMDEC, DIMDSEP, DIMZIN, DIMEXO, DIMEXE, DIMTAD, DIMATFIT, DIMTMOVE, DIMLWD, DIMCLRD, DIMCLRE, DIMCLRT, DIMLUNIT, DIMFRAC, DIMRND, DIMLFAC, DIMAUNIT, DIMADEC, DIMALT, DIMALTU, DIMALTD, DIMALTF, DIMALTRND | Set one value of the current dimension style (`Enter new value for DIMTXT <2.5000>:`). |
| LEADER | Draw a leader line with an arrowhead and annotation. |
| MLEADER (MLD) | Draw a multileader: arrowhead, leader line, landing and text. |
| MTEXT (MT) | Create a multiline (paragraph) text object. |
| QLEADER (LE) | Draw a quick leader with an arrowhead and annotation. |
| TEXTEDIT (ED) | Edit the contents of text objects, one after another (`[Undo/Mode]`; `TEXTEDITMODE` 1 edits one and ends). |
| PROPERTIES (PR) | Open the Properties palette to view and edit object properties. |
| DWGIN | Import geometry from a DWG file. |
| DWGOUT | Export the drawing to a DWG file. |
| PLOT | Plot or print the drawing to paper or PDF. |
| OPTIONS (OP) | Change application settings: update checks and the performance overlay. |
| LTSCALE (LTS) | Set the global linetype scale factor. |

## Draw

| Command | Alias | Status |
|---|---|---|
| LINE | L | Implemented -- `Specify next point or [Close/Undo]:`; **Close** joins back to the first point; Enter at the first prompt **continues** from the last line or arc (tangent to an arc: `Specify length of line:`) |
| PLINE (polyline) | PL | Implemented -- `Specify next point or [Arc/Close/Halfwidth/Length/Undo/Width]:`; **Arc** mode `[Angle/CEnter/CLose/Direction/Halfwidth/Line/Radius/Second pt/Undo/Width]` with each sub-step (an arc is tangent to the previous segment unless told otherwise; Ctrl bends it the other way); **Length** continues along the last segment; **Width** / **Halfwidth** ask for a starting and an ending value (typed, or a point's distance from the last vertex): the next segment tapers between them and the ending width stays in force, for later polylines too (`PLINEWID`); the band shows the arc and the width the click will make; Dynamic Input offers length and angle fields on a next-point pick |
| Polyline widths | -- | Two widths per vertex (start and end of the segment leaving it), drawn as filled bands (mitred between straight segments of one width), outlined with FILLMODE 0; picked anywhere on the band. MOVE / COPY / ROTATE / MIRROR / SCALE (the width scales) / STRETCH / ARRAY carry them; TRIM, BREAK, EXTEND, FILLET, CHAMFER and OFFSET hand them to the pieces (a taper keeps its slope); JOIN keeps each source's; EXPLODE drops them and says so. LIST, the Properties palette (Global width, Elevation, Thickness) and MATCHPROP (Polyline) know them. Saved in `.musa` (format 37) and DXF (40 / 41 / 43, 38, 39; the legacy POLYLINE's too); a wide polyline inside a block is filled where the block is inserted |
| CIRCLE | C | Implemented -- `Specify center point for circle or [3P/2P/Ttr (tan tan radius)]:`; centre + radius / `[Diameter]` with the last radius as the default (CIRCLERAD); **3P** through three points; **2P** by the ends of a diameter; **Ttr** tangent to two objects with a radius; **TTT** tangent to three objects (lines, circles, arcs, construction lines, polyline segments; the branch nearest the picks) |
| ARC | A | Implemented -- `Specify start point of arc or [Center]:`, `Specify second point of arc or [Center/End]:`; the **Center** branch (end, `[Angle/chord Length]`) and the **End** branch (centre, `[Angle/Direction/Radius]`), the centre first, **Continue** (Enter at the first prompt: tangent from the last line or arc), **Ctrl** at a pick switches the direction; the rubber band shows the arc the click will make |
| RECTANGLE | REC | Implemented |
| RECTANG (the command echoes as RECTANG) -- `Specify other corner point or [Area/Dimensions/Rotation]:`; length / width / area remembered as the next defaults; **Rotation** by an angle or `[Pick points]`, in force for later rectangles; `Current rectangle modes: Fillet=…` echoed; Area means the finished shape (corner cut-outs included); the band shows the rounded / chamfered corners | typed mid-command | Implemented |
| RECTANGLE options: **Dimensions** (`D` → length → width → quadrant-flip placement click), **Area** (`A` → area → `[Length/Width]` → side → placement), **Rotation** (`R` → angle) | typed mid-command | Implemented (option keywords, same state machine as CIRCLE `[Diameter]`) |
| RECTANGLE first-corner options: `[Chamfer/Elevation/Fillet/Thickness/Width]` | C / E / F / T / W | Implemented (Width is the polyline width; Elevation and Thickness are kept and saved, DXF 38 / 39, the view being 2D; all stay in force and are echoed in `Current rectangle modes:`) |
| SPLINE | SPL | Implemented (#23) -- Fit and CV methods |
| ELLIPSE | EL | Implemented (#23) -- axis-end, Center, Rotation, Arc |
| POLYGON (side count and Inscribed / Circumscribed remembered, 3..1024 sides, a typed radius stands the polygon on a flat bottom edge, an Edge-mode band) | POL | Implemented (#23) -- inscribed / circumscribed / Edge |
| POINT -- `Current point modes:  PDMODE=0  PDSIZE=0.0000`, `Specify a point:`, one point; **MULTIPLE** (`Enter command name to repeat:`) runs any command again until Esc, and the ribbon's Multiple Points is MULTIPLE POINT. **PDMODE** / **PDSIZE** / **PTYPE** set how points are drawn (0 a dot, 1 nothing, 2 a plus, 3 a cross, 4 a tick up; +32 in a circle, +64 in a square) and how big (drawing units; 0 or less a size on screen, about forty pixels at any zoom); kept in the drawing and in DXF | PO | Implemented (#23, #40) |
| XLINE / RAY | XL | Implemented (#23) -- Hor / Ver / Ang / Bisect / two-point |
| HATCH / BHATCH — **SOLID fill** of a region (Part A). Two boundary modes: **pick an internal point** (the engine traces the enclosing boundary from surrounding geometry — lines, arcs, circles, polylines — building a **planar arrangement** so a partitioning line correctly splits the region; closed entities inside become **islands/holes**) or **pre-select** closed polylines (noun-verb). "Valid hatch boundary not found." when no closed boundary encloses the pick. Fill is **derived, not baked** (rendered via the fill pipeline, so it plots as PDF vectors). Pickable (point-in-region, islands respected), PR-editable (Pattern/Scale/Angle/Origin), MATCHPROP-matchable, native + DXF round-trip. Selected hatch shows a **highlight tint over the fill + a grip at every boundary vertex** (drag to reshape) | H / BHATCH | Implemented (Part A: SOLID; line patterns = Part B) |
| HATCH **line patterns** (Part B) — `.PAT` parser + a built-in stock library (ANSI31–ANSI38, NET/NET3/GRID, BRICK, BOX, HEX/HONEY, ANGLE, DOTS, CROSS, SQUARE, TRIANG, GRASS, EARTH, STEEL, CONC, INSUL, …; authored from the public .PAT format, **not** copied from acad.pat — load that file for the vendor set). Line families are **generated + clipped to the boundary at render time** (derived-not-baked, even-odd so islands carve out) and plot as vectors. Choose via the command's `[Pattern/Scale/Angle]` options or the PR. SOLID stays the special fill name — one render path, patterns are not a fork | H ▸ Pattern | Implemented (Part B) |
| HATCH GRADIENT fills (entity colour to a second colour along an angle; 24 flat bands, so it plots as vectors; DXF gradient block both ways) | H ▸ Gradient | Implemented |
| HATCH **User defined** -- at the pattern prompt (seTtings, -HATCH Properties, HATCHEDIT Properties), `U`: `Specify angle for crosshatch lines <0>:`, `Specify spacing between the lines <1.0000>:`, `Double hatch area? [Yes/No] <No>:` -- parallel lines that far apart at that angle, crossed at right angles when double; in DXF as AutoCAD's user-defined type with its line families | H ▸ seTtings ▸ U | Implemented (#41) |
| GRADIENT -- HATCH with the gradient fill (its second colour and angle as last set; seTtings changes them): `Pick internal point or [Select objects/Undo/seTtings]:` | GD | Implemented (#41) |
| TRACE -- `Specify trace width <1.0000>:` (kept for the session), `Specify start point:`, `Specify next point:` until Enter: one polyline of that width | TRACE | Implemented (#45; a polyline, so it is mitred and edited as one) |
| SKETCH -- `Specify sketch or [Type/increment/toLerance]:`: press and drag to sketch; each stroke becomes lines, a polyline or a spline (`Enter sketch type [Line/Polyline/Spline]`), its points the increment apart, until Enter; one undo step | SKETCH | Implemented (#45) |
| ADDSELECTED -- `Select object:`: the command that draws that kind of object starts, and what it draws takes the object's layer, colour, linetype and lineweight (the current ones come back after) | ADDSELECTED | Implemented (#45; the type's own settings such as a text style or a hatch pattern are not copied yet) |
| BREAKLINE (Express Tools) -- `Specify first point for breakline or [Block/Size/Extension]:`, the second point, `Specify location for break symbol <Midpoint>:`: one polyline past both points by the extension with a zigzag break of the given size | BREAKLINE | Implemented (#77; the standard symbol only) |
| MOCORO (Express Tools) -- `Base point:`, then `Second point or [Copy/Base/Rotate/Scale/Undo] <eXit>:` again and again: the selection moved, rotated (`Rotation angle or [...]`) and scaled (`Scale factor or [...]`) in one command, the base going with a move; Copy makes the next ones copies, Undo takes the last back | MOCORO | Implemented (#77) |
| DIMEX / DIMIM (Express Tools) -- DIMEX: `Dimension styles to export <*>:` (* and ? wild cards), then a file to write (`.dim`); DIMIM: a file DIMEX wrote, then `Existing styles of the same name [Keep/Overwrite] <Keep>:` -- the dimension styles carried from one drawing to another | DIMEX / DIMIM | Implemented (#77; the file is Musa CAD's text format, not AutoCAD's .dim layout) |
| TFRAMES (Express Tools) -- the frames of wipeouts and images off together, or on together when the wipeout frames are off | TFRAMES | Implemented (#77) |
| TCOUNT (Express Tools) -- `Select objects:`, `Sort selected objects by [X/Y/Select-order] <Select-order>:` (left to right, top to bottom, or as selected), `Specify starting number and increment (Start,increment) <1,1>:`, `Placement of numbers in text [Overwrite/Prefix/Suffix/Find&replace..] <Prefix>:`: each text and multiline text gets its number instead of its text, before or after it (a space between), or in place of a search string (`Enter search string:`). The answers are offered again. One undo step | TCOUNT | Implemented (#77) |
| TEXTFIT (Express Tools) -- `Select Text to stretch or shrink:`, `Specify end point or [Start point]:`: the text's width factor set so it ends at the point, its start where it was or at a new starting point; Aligned and Fit text keep their own two points | TEXTFIT | Implemented (#77) |
| GATTE (Express Tools) -- `Select block or attribute [Block name]:` (or `Enter block name:`), the known tags listed, `Select attribute or type attribute name:`, `Enter new text:`, `Process all of them? [Yes/No] <Yes>:`: the attribute's value on every reference of the block, or only the one picked. One undo step | GATTE | Implemented (#77; an attribute is named by its tag, not picked) |
| VPSCALE (Express Tools) -- `Select the viewport to list the scale:`: `PS:MS == 1:2.5  Viewport Scale = 0.4` | VPSCALE | Implemented (#77) |
| TXTEXP (Express Tools) -- `Select objects:`: each text, multiline text and attribute definition among them becomes polylines -- along its letters' strokes in the built-in font, the underline and overline too, or closed round each glyph's outlines in a TrueType font -- on its layer and in its colour, as it is drawn (width factor, obliquing, Backwards and Upside down included). One undo step | TXTEXP | Implemented (#77) |
| FLATTEN (Express Tools) -- `Select objects to convert to 2d:`, `Remove hidden lines? <No>:`: the polylines' elevation and thickness set to 0 | FLATTEN | Implemented (#77) |
| BURST (Express Tools) -- EXPLODE for blocks whose attributes become text with the values the reference had (the hidden ones go) | BURST | Implemented (#77) |
| TEXTMASK (Express Tools) -- `Select text or [Masktype/Offset]:`: a wipeout behind each text, multiline text and attribute definition selected, the offset (text heights, 0.35 to begin with) clear of it | TEXTMASK | Implemented (#77; the mask is a wipeout, not grouped with its text) |
| CHANGE -- `Specify change point or [Properties]:`: lines' nearer ends moved to the point, circles through it; with texts selected `Specify new text insertion point <no change>:`, the style, height, rotation angle and text; with blocks the insertion point and rotation angle -- each for all of them at once; Properties runs CHPROP | CHANGE / -CH | Implemented (#55; AutoCAD asks the text and block settings object by object, showing each one's values) |
| HATCHEDIT -- `Select hatch object:`, `Enter hatch option [... Properties ... recreate Boundary ...] <Properties>:`: Properties sets the pattern (`?` lists them), scale and angle; recreate Boundary draws a closed polyline round each loop | HE / HATCHEDIT | Implemented (#41, #55; the other options not yet) |
| SPLINEDIT -- `Select spline:`, `Enter an option [Close/Join/Fit data/Edit vertex/convert to Polyline/Reverse/Undo/eXit] <eXit>:`: Edit vertex adds a control point into the nearest leg of the control polygon, deletes one (a spline keeps degree + 1) or moves one; convert to Polyline (`Specify a precision <10>:`); Reverse; Undo | SPE | Implemented (#55; Close, Join, Fit data, Elevate order and Weight not yet) |
| -MTEXT -- the MTEXT corners and options, then the text line by line at `MText:` until an empty line | -MTEXT / -T | Implemented (#43) |
| VIEW (V) opens the View Manager: the saved views, Set Current (or a double-click), New (the view on screen, named), Delete; -VIEW asks at the command line; NEWVIEW -- `Enter view name:` saves the view on screen | V / -VIEW / NEWVIEW | Implemented (#72; boundaries, layer snapshots and the view categories not yet) |
| LAYERSTATE (LAS) opens the Layer States Manager: the named layer states, New, Restore (or a double-click), Rename, Delete; -LAYERSTATE asks at the command line | LAS / -LAYERSTATE | Implemented (#69; Import / Export not yet) |
| FIND -- the Find and Replace window: find what, replace with, the entire drawing or the selection, match case, whole words only; Find selects the objects found, Replace All changes them in one undo step. Text, multiline text, leaders, multileaders, table cells, block attributes and dimension text overrides. Without the window: `Enter the text to find:`, `Enter the replacement text, or Enter to find only:`, `Match case [Yes/No] <No>:`, `Find whole words only [Yes/No] <No>:` | FIND | Implemented (#62) |
| TCASE (Express Tools) -- `Enter case [Sentence/Lower/Upper/Title/tOggle] <Upper>:` for the selected text | TCASE | Implemented (#77) |
| SOLID -- `Specify first point:`, `second`, `third`, `Specify fourth point or <exit>:`; the third and fourth points are across from the first two (four in a Z make a bow tie, as AutoCAD), and go on as the first two of the next; Enter after three makes a triangle | SO | Implemented (#45; each a solid hatch) |
| BOUNDARY -- `Pick internal point:` until Enter: closed polylines round the area HATCH's pick point would fill, and round its islands | BO / -BOUNDARY | Implemented (#45; curved edges come out as short straight segments; no dialog or Region type) |
| CENTERMARK -- `Select circle or arc to add centermark:` until Enter: a cross at the centre and centre lines past the circle in the Center linetype. CENTERLINE -- `Select first line:`, `Select second line:`: the centre line from the midpoint of their starts to the midpoint of their ends, 3.5 past each, in the Center linetype | CM / CL | Implemented (#45; plain lines, not associative) |
| WIPEOUT (points, or [Polyline] from a closed polyline with optional erase; [Frames] ON/OFF/Display but not plot = WIPEOUTFRAME 1/0/2, saved with the drawing). Masks lines, curves and hatches beneath it; text always shows through (DRAWORDER is not modelled). DXF: a colour-7 solid | WIPEOUT | Implemented |
| FIELD (Date, Time, Filename, Login) as `%<Date>%`-style codes in any text, expanded at layout and refreshed on every regen | FIELD | Implemented |

## Modify

| Command | Alias | Status |
|---|---|---|
| ERASE (`Select objects:` with every keyword; a pre-selection erased at once; OOPS restores the set) | E | Implemented (#53) |
| UNDO | U | Implemented |
| REDO | — (Ctrl+Y) | Implemented; **MREDO** -- `Enter number of actions or [All/Last]:` |
| REVERSE -- `Select objects:`; lines, polylines (their arcs and widths with them) and splines run the other way | REVERSE | Implemented (#55) |
| OVERKILL -- `Select objects:`, then `Current settings: Tolerance=1e-06, Combine partial overlap=Yes, Combine end to end=No` and `Enter an option to change [Done/tOlerance/combine parTial overlap/combine Endtoend] <done>:`: duplicates (points, lines either way round, circles, arcs, polylines, single-line text of the same properties, within the tolerance) are deleted, and collinear lines that overlap -- with Endtoend, that meet too -- become one line. One undo step; the settings are kept for the session | -OVERKILL | Implemented (#55; no dialog, Ignore property or polyline optimising yet) |
| COPYTOLAYER -- `Select objects to copy:`, `Select object on destination layer or [Name] <Name>:` (a layer named that does not exist is made), `Specify base point or [Displacement/eXit] <eXit>:` and the second point: the copies, on that layer, moved | COPYTOLAYER | Implemented (#55) |
| CHPROP -- `Select objects:`, then `Enter property to change [Color/LAyer/LType/ltScale/LWeight]:` as often as you like (a colour by number, name, BYLAYER or Truecolor `Red, Green, Blue:`; Continuous, Dashed, Center or Hidden; a lineweight in mm); Enter makes the changes, one undo step | CHPROP | Implemented (#55; Thickness, Transparency, Material and Plotstyle are not modelled) |
| BLEND -- `Current setting: Continuity = Tangent`, `Select first object or [CONtinuity]:`, `Select second object:`: a spline from the end of each object nearer the pick, leaving along the object's own direction -- **Tangent** a cubic, **Smooth** a quintic straight where it leaves; lines, arcs, open polylines, splines and elliptical arcs | BLEND | Implemented (#55) |
| MOVE | M | Implemented -- `Specify base point or [Displacement] <Displacement>:`, `Specify second point or <use first point as displacement>:`; the last displacement is the next default |
| COPY | CO / CP | Implemented -- `Current settings: Copy mode = Multiple`, `[Displacement/mOde]`, `Specify second point or [Array] <use first point as displacement>:` then `[Array/Exit/Undo] <Exit>:`; **Array** (`Enter number of items to array:`, `[Fit]`), **Undo** takes the last copy back, every copy its own undo step; **mOde** Single / Multiple kept for the session |
| MIRROR (mirrored text stays readable, `MIRRTEXT` 0; `MIRRTEXT` 1 reflects it) | MI | Implemented |
| OFFSET (line/circle/arc) | O | Implemented -- `Current settings: Erase source=No  Layer=Source  OFFSETGAPTYPE=0`, `Specify offset distance or [Through/Erase/Layer] <last>:` (a value, two points, or **Through**), `Select object to offset or [Exit/Undo] <Exit>:`, `Specify point on side to offset or [Exit/Multiple/Undo] <Exit>:`; **Multiple** steps out from the offset just made, **Undo** takes one back, every offset its own undo step; **Erase** removes the source, **Layer** Current / Source; the settings are kept for the session. The offset takes the source's layer, colour, linetype, lineweight and linetype scale (the current layer with Layer = Current), and is shown under the cursor at the side prompt |
| OFFSET (construction line, ray) -- a parallel one of the same kind | O | Implemented |
| OFFSET (ellipse, elliptical arc, spline) -- a spline a constant distance from the source, as AutoCAD makes it; a distance past the curve's tightest radius on the inside is refused ("the offset would cross itself") | O | Implemented |
| OFFSETGAPTYPE 0 / 1 / 2 (extend / fillet / chamfer the gaps at a polyline's outside corners) | OFFSETGAPTYPE | Implemented |
| OFFSET (polyline, incl. closed rectangles + bulged/filleted corners) — each segment offset (lines parallel, arcs concentric with the bulge preserved) and **corners re-mitered** as the intersection of adjacent offset curves (line/line, line/arc, arc/arc via the shared line_line / line_circle / circle_circle primitives), so edges stay at distance d with clean corners (no trapezoid). Over-large offsets that would fold the shape fail gracefully ("Offset distance too large for this polyline.") leaving the geometry unchanged | O | Implemented |
| ROTATE | RO | Implemented -- `Specify rotation angle or [Copy/Reference] <0>:`; the selection turns with the cursor (an engine-side band: every kind, at the current zoom) with the live angle shown at the cursor; **Reference** by a value or two points, then `Specify the new angle or [Points] <0>:` |
| SCALE | SC | Implemented -- `Specify scale factor or [Copy/Reference]:`; dragging scales by the cursor's distance from the base point in drawing units (AutoCAD's rule: one unit away is a factor of 1), previewed by the engine with the live factor at the cursor; **Reference** by a value or two points, then `Specify new length or [Points] <1.0000>:` |
| ARRAY (asks the type: Rectangular / PAth / POlar) | AR, -ARRAY | Implemented |
| ARRAYRECT (rows, columns, spacings, angle of axes) | ARRAYRECT | Implemented |
| ARRAYPOLAR (centre, count, fill angle, rotate items) | ARRAYPOLAR | Implemented |
| ARRAYPATH (Divide / Measure, align to path) | ARRAYPATH | Implemented |
| ARRAYEDIT / ARRAYCLOSE | - | Not applicable (arrays are non-associative -- see below) |
| POLYGON (centre: Inscribed/Circumscribed; Edge) | POL | Implemented |
| RECTANGLE [Chamfer] / [Fillet] corner options (session defaults, last set wins) | REC | Implemented |
| RECTANGLE [Width] / [Elevation] / [Thickness] | REC | Implemented |
| REVCLOUD (Arc length, Object + Reverse direction, Rectangular, Polygonal, Freehand by a drag or a clicked path) -- `Minimum arc length: ...   Style: Normal   Type: Freehand`; the type is remembered and the prompt follows it (`Specify first corner point` / `Specify start point` / `Specify first point or [Arc length/Object/Rectangular/Polygonal/Freehand/Style] <Object>:`); **Style** Calligraphy draws each lobe from nothing to a fifth of its chord wide; with Freehand, a press and drag from the start point draws the cloud along the cursor, closed when the drag ends back near its start and open otherwise | REVCLOUD | Implemented (#40; Modify not offered) |
| EXPLODE (polyline -> lines/arcs; block one level; dimension/leader -> lines, solids, text; hatch -> lines or boundary; MTEXT -> TEXT per line; table -> lines + text) | X | Implemented |
| PURGE (unused layers) | PU | Implemented |
| PURGE (every table: layers, dimstyles, text styles, blocks, fonts, table styles, images; the dialog with Confirm each item; -PURGE with names, wild cards and Verify; zero-length geometry and empty text objects) | PU / -PURGE | Implemented (#30, #53) |
| ALIGN (one pair moves; two pairs align with the scale question; `Specify third source point or <continue>:`; rubber lines from each source point) | AL | Implemented |
| LENGTHEN -- `Select an object to measure or [DElta/Percent/Total/DYnamic] <Total>:` (a pick reports `Current length: ...`, and an arc's included angle); **DElta** / **Total** take a length or **Angle** (an arc's), **Percent** a percentage, **DYnamic** `Specify new end point:`; then `Select an object to change or [Undo]:` one object after another, the end nearer the pick moving. Lines, arcs, open polylines (the end segment takes the change, straight or round its arc) and elliptical arcs; the mode and amounts are kept for the session | LEN | Implemented (splines not yet) |
| BREAK (line, arc, circle, open + closed polyline) | BR | Implemented |
| BREAKATPOINT (split, no gap) | BREAKATPOINT | Implemented |
| ELLIPSE (axis-end / Center / Rotation / Arc by angle, parameter, included) | EL | Implemented; the Rotation step previews the ellipse turned by the cursor's angle. **PELLIPSE** 1 makes it a closed polyline of 32 arcs (an elliptical arc an open one), tangent to the ellipse at 16 points |
| SPLINE (Fit with Knots; CV with Degree; Undo; Close) | SPL | Implemented (Tangency, fit tolerance, Object deferred) |
| XLINE (Hor / Ver / Ang / Bisect / two-point) | XL | Implemented (Offset deferred) |
| DONUT | DO | Implemented (a closed two-arc wide polyline, as AutoCAD makes it; FILLMODE decides the fill) |
| FILL / FILLMODE / PLINEWID | -- | Implemented |
| VIEW Save/Restore/Delete/?/Window | V | Implemented (Orthographic/Ucs are 3D-only) |
| GROUP / UNGROUP / PICKSTYLE 0..3 / -GROUP (?, Order, Add, Remove, Explode, REName, Selectable, Create) / GROUPEDIT / Ctrl+H | G | Implemented (#53; group membership persists in .musa; not on the undo stack; PICKSTYLE's hatch bit waits for associative hatches, #41) |
| UNITS (the Drawing Units dialog; -UNITS with AutoCAD's numbered tables; INSUNITS; degrees/DMS/grads/radians/surveyor) -- lengths are typed as decimals, fractions (`12 3/8`, `3/8`) or feet and inches (`1'-6"`, `1'6`, `6"`, `1'-6 1/2"`; a foot is twelve drawing units) at every length and point prompt; DXF carries `$INSUNITS`, `$LUNITS`, `$LUPREC`, `$AUNITS`, `$AUPREC`, `$ANGDIR`, `$ANGBASE` and `$MEASUREMENT` both ways | UN / -UNITS | Implemented (#67, #81; dimension text keeps the dimstyle precision, as DIMLUNIT does; blocks and pastes are not yet scaled by the units) |
| -DWGUNITS -- `Unit for length <3>:` (Inches, Feet, Millimeters, Centimeters, Decimeters, Meters), the display format and precision, then `Scale objects in current drawing to reflect change in units? [Yes/No] <Yes>:`: every model-space object and the limits scaled to the new unit, one undo step | -DWGUNITS | Implemented (#81) |
| LTSCALE shows the current value and says `Regenerating model.`; CELTSCALE, PSLTSCALE (viewport dashes follow the viewport scale), MSLTSCALE (recorded) | LTS | Implemented (#67) |
| STYLE (text-style table; width factor and oblique applied to TEXT; DXF STYLE table both ways) -- STYLE opens the Text Style window (the styles, font, height, width factor, oblique angle, Upside down and Backwards; Set Current, New, Apply); -STYLE asks at the command line | ST / -ST | Implemented (vertical deferred; MTEXT keeps fonts directly) |
| BLOCK / INSERT / WBLOCK (lines, circles, arcs, polylines, text, mtext, nested inserts) | B / I / W | Implemented |
| Block attributes: ATTDEF entities become attributes on BLOCK; INSERT prompts for each value (Constant/Preset skipped); ATTDISP; ATTEDIT; EXPLODE gives the definitions back; native v28 and DXF ATTDEF / INSERT+ATTRIB+SEQEND both ways | ATT / ATTDISP / ATTEDIT | Implemented (multi-line attributes are not) |
| Attribute dialogs: EATTEDIT (a reference's values, also on double-click) and BATTMAN (a definition's attributes, synced to its references by tag); the Insert tab's Block and Block Definition panels | EATTEDIT / BATTMAN | Implemented |
| REFEDIT / REFSET / REFCLOSE: in-place block editing through a working set, mapped back into the definition's frame on save (needs a positive, uniform scale); Discard restores the reference; undo takes back the save as one step | REFEDIT | Implemented (no fading of the rest of the drawing during the edit) |
| Layouts / paper space: a layout table with page setups, per-entity space (model or a layout), only the active space drawn / picked / edited, the sheet drawn under a layout, layout tabs (click, right-click New/Rename/Delete), PLOT of a layout = its sheet at 1:1, native v30, DXF paper-space flag (67) both ways | LAYOUT / MODEL / PSPACE | Implemented |
| Viewports (MVIEW): a paper-space entity showing model space at a scale, derived at snapshot time (model geometry mapped into the sheet and clipped to the frame; text, hatches, wipeouts and whole images included), plotted with the sheet, grips, native v31 and DXF VIEWPORT both ways | MVIEW | Implemented (per-viewport layer freezing: VPLAYER, below) |
| Tiled model-space viewports (VPORTS): the window split into tiles each with its own camera, the active one outlined, tile-local picking and dynamic input, the wheel on any tile, Join, named configurations, native v35 and the DXF VPORT table (`*Active` entries) both ways | VPORTS | Implemented |
| Per-viewport layer freezing: a viewport's own frozen-layer list applied when the model is built for it (and while editing through it with MSPACE), the Layer Properties Manager's VP Freeze / New VP Freeze columns, native v34, DXF VIEWPORT 331 + LAYER flag 2 | VPLAYER | Implemented |
| MSPACE / PSPACE through a viewport: double-click in / out, the camera hand-off keeps the viewport's on-screen size, the view left on PSPACE becomes the viewport's view | MSPACE | Implemented (the sheet is not shown around the model while inside; a tab switch drops the edit without a write-back) |
| PEDIT (Close/Open/Join/Width/Edit vertex/Fit/Spline/Decurve/Reverse/Undo) -- a line or arc picked asks `Object selected is not a polyline` / `Do you want to turn it into one? <Y>`; **Fit** puts two arcs on each segment, through every vertex and smooth at each (Decurve straightens them, keeping the vertices Fit added); **Multiple** (`Select objects:`) applies Close, Open, Width, Fit, Spline, Decurve and Reverse to every polyline selected, lines and arcs made polylines without asking, and its **Join** asks `Enter fuzz distance <0.0000>:`; **Edit vertex** -- `Enter a vertex editing option [Next/Previous/Break/Insert/Move/Regen/Straighten/Tangent/Width/eXit] <N>:` with an X on the current vertex: Next and Previous walk it, Insert puts a vertex after it, Move moves it, Width sets the next segment's, Break and Straighten mark it and `[Next/Previous/Go/eXit]` go to a second vertex -- Go breaks the polyline between the two or makes their run one straight segment | PE | Implemented (Tangent is accepted and not kept; Spline yields a SPLINE entity). **Ltype gen** -- `Enter polyline linetype generation option [ON/OFF] <Off>:`: off, the dash pattern starts again at each vertex; on (a new polyline here), it runs on across them; DXF 70 bit 128 |
| Object snaps: Insertion, Apparent intersection, Parallel, Extension, Geometric Center; OSNAP settings dialog; -OSNAP; OSMODE | OS | Implemented (the running snaps start as AutoCAD's default set) |
| One-time object snaps typed at a point prompt (`END MID CEN GCEN NOD QUA INT EXT INS PER TAN NEA APP PAR NON`), `FROM`, `M2P` / `MTP`, `TT`, `TK`; the Shift + right-click object snap menu | typed mid-command | Implemented (#66) |
| ROTATE/SCALE [Copy]/[Reference]; Rotate/Scale value dialogs with live ghost | RO / SC | Implemented |
| Properties palette: feature control frame cells and datum letter editable | PR | Implemented |
| Properties palette: editable Start/End/Center X-Y, Radius, Position X-Y | PR | Implemented |
| DXF TOLERANCE: feature control frames both ways (cells joined by %%v, GD&T symbols as {\\Fgdt;x}); datum symbols written as one-cell frames | DXFOUT/DXFIN | Implemented (a datum's leader triangle has no DXF form) |
| AUDIT | AUDIT | Implemented |
| PURGE by type and by name (blocks, dimstyles, table styles, image defs, groups, layers, text styles) | PU | Implemented |
| RAY (start + through points) | RAY | Implemented |
| POINT | PO | Implemented |
| DIVIDE (n equal segments) -- `Enter the number of segments or [Block]:`; **Block** (`Enter name of block to insert:`, `Align block with object? [Yes/No] <Y>:`) puts block references at the marks, turned to the curve when aligned | DIV | Implemented |
| MEASURE (fixed interval) -- `Specify length of segment or [Block]:`, from the end nearer the pick (a closed curve from its start); Block as DIVIDE | ME | Implemented |
| TRIM a line (cut by line/circle/arc edges) | TR | Implemented |
| TRIM an arc/circle *entity* | TR | Implemented (a trimmed circle becomes an arc) |
| TRIM a polyline *entity* (open or closed, arc segments cut exactly) | TR | Implemented |
| EXTEND a line (to line/circle/arc boundary) | EX | Implemented |
| EXTEND an arc *entity* | EX | Implemented |
| EXTEND an open polyline *entity* (straight end segment; polylines also act as boundaries) | EX | Implemented (an arc end segment is refused) |
| TRIM / EXTEND Quick mode (TRIMEXTENDMODE 1): every object is a cutting edge or boundary; `Current settings: Projection=UCS, Edge=None, Mode=Quick`, `Select object to trim or shift-select to extend or [cuTting edges/Crossing/mOde/Project/eRase]:` (EXTEND: `[Boundary edges/Crossing/mOde/Project]`) | TR / EX | Implemented -- press on empty space and drag: the freehand path trims (or extends) everything it crosses, where it crosses, as one undo step; two clicks on empty space draw a straight fence; **Fence** takes typed points until Enter; an object with nothing to trim it to is deleted, as in AutoCAD; **Shift** swaps trim and extend for that pick or path; **Undo** takes the last one back |
| TRIM / EXTEND Standard mode (mOde, TRIMEXTENDMODE 0): `Select cutting edges ...` / `Select boundary edges ...`, `Select objects or <select all>:`, then `[cuTting edges/Fence/Crossing/mOde/Project/Edge/eRase/Undo]` | TR / EX | Implemented -- Enter takes every object; objects selected beforehand are the edges; nothing is deleted; two clicks on empty space draw a crossing window; **Edge** `[Extend/No extend]` (EDGEMODE) lets a chosen line edge cut along its whole infinite line and an arc edge round its circle; a trimmed edge stays an edge in its pieces |
| TRIM / EXTEND on more kinds | TR / EX | Implemented -- ellipses and elliptical arcs (a whole ellipse trims to an elliptical arc; an arc extends round its ellipse); construction lines and rays (trimmed to rays, or a ray to a line and a ray; as cutting edges and boundaries everywhere, the path and window included); a polyline's arc end extends round its circle. Hovering an object at the prompt draws the part a pick would trim away (Quick mode: the whole object when nothing cuts it) or the extension it would add; Shift shows the other. Splines are not trimmed or extended yet |
| TRIM / EXTEND options | TR / EX | **Crossing**: `Specify first corner:` / `Specify opposite corner:` -- each object crossing the window is taken once, where the window's edge first meets it walking clockwise from the first corner, and what lies wholly inside is taken at its middle; **eRase** (TRIM): `Select objects to erase or <exit>:`, its own undo step; **Project** `[None/Ucs/View]` (PROJMODE; a 2D drawing is the same in all three); TRIMEXTENDMODE, EDGEMODE and PROJMODE typed as commands |
| FILLET (line/line; radius 0 or tangent arc) | F | Implemented -- `Current settings: Mode = TRIM, Radius = …`, `Select first object or [Undo/Polyline/Radius/Trim/Multiple]:`, `Select second object or shift-select to apply corner or [Radius]:`; the radius and Trim mode are remembered; **Multiple** repeats with one undo step per corner, **Undo** takes the last one back, **No trim** adds only the arc, **Polyline** rounds every corner, **Shift** at the second pick makes a sharp corner; the trimmed objects keep their layer, colour and linetype |
| FILLET parallel lines, construction lines, polylines with lines | F | Implemented -- a line and an open polyline's straight end segment, or two polylines' end segments, are joined at their corner into one polyline with the corner rounded (No trim: only the arc is added); two parallel lines are joined by a half circle as wide as the gap at the end of the first line nearer its pick, the second trimmed or extended to meet it (the radius does not apply); a construction line or ray fillets with a line as the line it runs along, and what stays of it is a ray. At the second-object prompt of FILLET and CHAMFER the hovered object shows what a pick would make -- both objects as they would be and the arc or bevel (Shift: the sharp corner) |
| FILLET (polyline corner → a true arc segment / bulge, dimensionable) | F | Implemented (incl. RECTANGLE corners — a rectangle IS a closed polyline; verified end-to-end through the full F-command path, including the closing-edge wrap corner) |
| Polyline arc segments (per-vertex bulge, AutoCAD LWPOLYLINE) | — | Implemented |
| FILLET line/arc, line/circle, arc/arc, arc/circle, circle/circle (radius > 0; nearest to the picks) | F | Implemented |
| CHAMFER (line/line; Distance or Angle method) | CHA | Implemented -- `(TRIM mode) Current chamfer Dist1 = …, Dist2 = …`, `Select first line or [Undo/Polyline/Distance/Angle/Trim/mEthod/Multiple]:`, `Select second line or shift-select to apply corner or [Distance/Angle/Method]:`; distances, length and angle, method and Trim mode remembered; **Multiple**, **Undo**, **No trim**, **Polyline** (every corner), **Shift** for a clean corner; the trimmed lines keep their properties |
| CHAMFER (polyline corner) | CHA | Implemented |
| JOIN — `Select source object or multiple objects to join at once:` then `Select objects to join:` (picks and windows), or objects selected beforehand. Lines along one line become ONE line (gaps and all), arcs on one circle one arc (counter-clockwise from the source; round the whole circle, a circle), elliptical arcs on one ellipse one elliptical arc; for an arc or elliptical arc source, **cLose** makes it whole. Mixed lines/arcs/open polylines merge every connected chain into ONE polyline each (arcs become bulged segments), inheriting the source's layer/properties; a chain whose ends meet becomes a **closed** polyline. One undo group | J | Implemented (splines not yet) |
| ARRAY dialog (parametric grid / polar / path with a live preview) | AR | Implemented (Ph11 ParameterDialog) |
| **STRETCH** — AutoCAD's flow, prompt for prompt: `Select objects:` (pick, window or crossing drags accumulate with "N found"; **Enter or right-click** ends it; a pre-selected set skips it) → `Specify base point or [Displacement] <Displacement>:` → `Specify second point or <use first point as displacement>:`, with the selection **stretched live under the cursor** (ortho-aware). AutoCAD's rule decides what moves: an object *crossed* by a crossing window has only the vertices inside it moved; one fully enclosed, or selected by a pick or an ordinary window, moves whole; a line that merely passes through the window is left alone. An arc endpoint moves with the arc's height above its chord preserved; circles and single-point kinds (text, insert, image, table, GD&T) move if their centre/anchor is caught and are never deformed; a dimension whose def points move **re-measures**. One undo group; reports what it did or why nothing moved | S | Implemented (issue #24) |
| **DIST (DI)** — distance, angle and delta X/Y between two points. Answered from the picked points alone, so it never reaches the store | DI | Implemented (issue #30) |
| **ID** — the coordinates of a point | ID | Implemented (issue #30) |
| **AREA (AA)** — area and perimeter of a closed object (exact for circles, shoelace over the SAME tessellation the renderer draws for everything else). An OPEN object reports its length and says it has no area, rather than the area of the polygon you would get by closing it | AA | Implemented (issue #30) |
| **LIST (LI)** — an object's type, layer and defining parameters. A dimension reports its MEASURED value, which by construction cannot have been authored | LI | Implemented (issue #30) |
| EXPLODE | X | Implemented (#25) -- a block reference one level, a polyline into segments |
| JOIN | J | Implemented (see the row above) |

### The ARRAY family, and why ARRAYEDIT is absent

AutoCAD ships seven array commands. Four of them create an array and are implemented
here in full: `ARRAY`/`-ARRAY` (which ask for the type), `ARRAYRECT`, `ARRAYPOLAR` and
`ARRAYPATH`. As in AutoCAD, `PA` selects Path and `PO` selects Polar; a bare `P` is
ambiguous and is refused rather than guessed at.

`ARRAY` keeps AutoCAD's legacy four rectangular prompts (rows, columns, row spacing,
column spacing) unchanged. The axis angle is offered by the modern `ARRAYRECT`, which
matches where AutoCAD puts it and leaves existing muscle memory alone. The angle
rotates the row/column **axes**, not the items: a rotated rectangular array is a skewed
lattice of upright copies.

`ARRAYPATH` supports both of AutoCAD's distribution methods:

- **Divide** -- spread N items over the whole path. On an open path the first and last
  items sit on the endpoints; on a closed path the items divide by N rather than N-1, so
  nothing is doubled at the seam.
- **Measure** -- place an item every given distance, optionally capped by an item count.
  Stations never run past the end of the path.

Aligned items turn to follow the path tangent, measured relative to the tangent at the
first station, so item 1 keeps the orientation you drew and the rest follow the curve.
The path curve is left in the drawing and may not itself be part of the selection.

The remaining two, `ARRAYEDIT` and `ARRAYCLOSE`, edit an **associative** array -- a
parametric entity that remembers its source objects and parameters so the pattern can be
re-driven later. Musa CAD's arrays are non-associative: they produce ordinary
independent copies, exactly as AutoCAD's own `-ARRAY` does. There is therefore no
association for `ARRAYEDIT` to reopen and no array-editing state for `ARRAYCLOSE` to
leave. Adding them means adding associative arrays first, which is a data-model change
(a new parametric entity kind), not a command.

## Annotate / Dimensions

| Command | Alias | Status |
|---|---|---|
| TEXT (single-line) | DT / TEXT | Implemented -- `Current text style: "Standard"  Text height: 2.5000  Annotative: No  Justify: Left`; `Specify start point of text or [Justify/Style]:` (a justification may be typed there too); **Justify** `[Left/Center/Right/Align/Middle/Fit/TL/TC/TR/ML/MC/MR/BL/BC/BR]`, each asking for its own point, Align and Fit for the two ends of the baseline (Align scales the height to reach, Fit keeps it and squeezes the width); **Style** by name (`?` lists); `Specify height <last>:` and `Specify rotation angle of text <last>:` take a value or a point and are remembered, with the justification; then the text line after line, each Enter starting the next line under the last (its own undo step), an empty line ending the command. The line shows on the canvas where it will stand while it is typed (spaces and capitals as typed) |
| Text justification | -- | The text entity keeps AutoCAD's fifteen justifications, the second point of Aligned / Fit and a width factor of its own; drawing, bounds, picking, grips (both ends of an aligned text) and the edit box share one layout. Saved in `.musa` (format 38) and DXF (72 / 73, 11 / 21, 41); a text justified on its second point in a DXF from elsewhere is read where it stands |
| JUSTIFYTEXT / SCALETEXT / TXT2MTXT / TEXTEDITMODE | -- | Implemented |
| **MTEXT (paragraph text; two-corner box → wraps within the width)** -- `Specify opposite corner or [Height/Justify/Line spacing/Rotation/Style/Width/Columns]:`: the height (the current text style's by default), the justification the text hangs from in the box, line spacing as a factor (`1.5x`) or a distance, rotation, a text style, or a width in place of the corner | MT / MTEXT / T | Implemented (multi-line + paragraph fields; inline per-char formatting Planned) |
| MTEXT fields: defined width, height, width factor, line spacing, attachment (TL..BR), rotation, ByLayer/override colour | — | Implemented (discrete, queryable — Properties palette ready) |
| MTEXT grips: insertion (move) + width (re-wraps live) | — | Implemented |
| **QLEADER (arrow → leader vertices → attached MTEXT label)** | LE / QLEADER / QL | Implemented (label is owned → moves with the leader) |
| QLEADER grips: arrow tip + each vertex + text position | — | Implemented |
| **Double-click a TEXT / MTEXT / QLEADER label → edit its content** (dark modal editor, pre-filled) | (double-click) | Implemented (Ph21) |
| **TEXTEDIT / DDEDIT** (`Select an annotation object or [Undo/Mode]:` → type new content; repeats until Enter, each edit its own undo step; Mode Single / Multiple) | ED / TEXTEDIT / DDEDIT | Implemented |
| Text edit = one undo group, preserves layer/properties/position (not delete+recreate) | — | Implemented (Ph21) |
| **AutoCAD control codes in TEXT & MTEXT** — `%%d`→°, `%%p`→±, `%%c`→⌀, `%%b`→⌴ counterbore, `%%h`→↧ depth, `%%v`→⌵ countersink, `%%%`→literal %, `%%nnn`→char by code, `%%o`/`%%u`→overline/underline toggles, `\U+XXXX`→any Unicode code point (**universal**, not MTEXT-only). Codes are **stored raw and expanded at render time** (editing shows the raw codes; save/load + DXF round-trip them). Shared with the Leader/MLeader labels | (type in any text) | Implemented |
| **Drafting symbols in the stroke font** — hole callouts (⌴ counterbore/spotface, ↧ depth, ⌵ countersink), the 14 GD&T characteristics (straightness, flatness, circularity, cylindricity, profile of a line/surface, angularity, perpendicularity, parallelism, position, concentricity, symmetry, circular/total runout), the 7 material-condition modifiers (Ⓜ Ⓛ Ⓢ Ⓟ Ⓕ Ⓣ Ⓤ), plus □ square, conical taper and slope. Same 6×8 cell and **same monospace advance** as the letters, so layout/bounds/pick are unchanged; works in TEXT, MTEXT, LEADER and dimension text alike. Hand-authored, not traced from any font | `%%b`/`%%h`/`%%v` or `\U+XXXX` | Implemented (issue #9) |
| **Leader / MLeader label properties in PR + MATCHPROP** — selecting a Leader or MLeader shows a Text section (Contents, Height, Font; MLeader also Width factor / Line spacing / Attachment); MATCHPROP copies the label font/height across the whole text family (TEXT ↔ MTEXT ↔ Leader ↔ MLeader) | PR / MA | Implemented |
| **Leader / MLeader arrow properties in PR + MATCHPROP** — a Leader section with **Arrowhead** (type) + **Arrow size**, each ByStyle or a per-leader override (override-first-else-dimstyle, like dimension overrides); native round-trip; MATCHPROP copies the arrow leader↔leader | PR / MA | Implemented |
| **Leader / MLeader label Text color** — a per-leader override so the label colours independently of the leader line + arrow (General colour); ByStyle = the entity colour; native round-trip; MATCHPROP leader↔leader | PR ▸ Text color | Implemented |
| **Properties palette (PR): dockable, context-sensitive panel for the selection** | PR / PROPERTIES / PROPS / CH | Implemented (Ph22) |
| PR multiplicity: nothing / one / many-same / many-mixed, with **\*VARIES\*** where values differ; edits set all | — | Implemented (Ph22) |
| PR universal props: **Layer / Color / Linetype / Lineweight** (ByLayer or override) editable single + multi + mixed | — | Implemented (Ph22) |
| PR Geometry group (editable since issue #32): line length/ends, circle/arc center+radius, text position | — | Implemented (#32) |
| PR full **Text / MTEXT** group: contents, height, rotation, justify, width factor, line spacing, defined width, attachment, **font** | — | Implemented (Ph22; font dropdown real in Ph29) |
| **Font** dropdown (Standard stroke font + system TrueType/OpenType faces); switching re-renders the selected text as one undo group (varies/set-all) | PR Font | Implemented (Ph29) |
| Imported text fonts: TTF-by-name resolves to the installed face (filled glyphs); single-stroke SHX fonts (romans/simplex/isocp/txt…) render with the built-in single-stroke font (faithful match); missing → stroke fallback (true SHX binary parsing staged) | — | Implemented (Ph29) |
| PR deep **Dimension** group: per-dimension overrides — arrowhead type/size, dim-line & ext-line color, text height/color/placement, precision (each ByStyle or Overridden, with reset-to-style) | — | Implemented (Ph24) |
| PR **Dimension** text decoration — text prefix / suffix (raw, so `%%c` and `6X` work) and a tolerance mode (none / symmetric / limits / basic / reference) with upper & lower deviations. The measured VALUE stays computed from the def points and is never authorable; only the decoration around it is. **Not** MATCHPROP-matchable by design — a fit class is semantics about THIS feature, not presentation | — | Implemented (issue #7) |
| PR **Dimension** **Text override** — AutoCAD's `<>` field: `<>` expands to the measurement so the override tracks the geometry (`<> H7`, `2x<>`); an override without `<>` replaces the value, shown explicitly rather than silently. Supersedes prefix/suffix and the deviation-deriving tolerance modes; Basic/Reference still frame it. Feeds the ISO 129-1 fit like any other label | — | Implemented (issue #20) |
| **ISO 129-1 narrow-dimension fit** — when the value and/or the arrowheads do not fit between the extension lines, the value is placed outside past the extension line and the arrowheads flip to point inward from beyond it. Text and arrows are tested **independently** (so arrows can stay inside while the value moves out). The test measures the fully decorated, code-substituted label (widest line when Limits stacks two). PR row **Text fit** = ByStyle / Auto / Always inside / Always outside; MATCHPROP-matchable (placement is presentation). Default Auto at both style and override level, so existing drawings simply stop colliding. Linear/Aligned; radial forms deferred | PR ▸ Dimension ▸ Text fit | Implemented (issue #12) |
| **TOLERANCE / TOL** — a GD&T **feature control frame**: an ordered cell list (characteristic, tolerance value, up to three datum references), drawing its own borders and dividers with uniform cell height derived from the DIMSTYLE text height (ASME Y14.5 proportions). Cells are ordinary text, so `\\U+2316` and `%%c` reach the stroke font's GD&T glyphs. Selectable as a unit (click anywhere inside), grip-movable, PR-editable (text height / colours), MATCHPROP-matchable with dimensions, native round-trip. **DXF: not written — stated gap** | TOL | Implemented (issue #8) |
| **DATUM / DIMDATUM** — a datum feature symbol: boxed datum letter + leader + **filled** triangle at the feature. Shares DIMSTYLE/overrides with dimensions; the leader attaches to whichever box edge faces the feature. Grips on both the box and the tip | DATUM | Implemented (issue #8) |
| **TABLE** (TB) opens the **Insert Table** window: the table style, the columns and their width, the data rows and their height in lines, and whether the first row is a Title and the next a Header; then `Specify insertion point:`. **-TABLE** asks it all on the command line (below) | TB / TABLE | Implemented (#44; Auto, from a data link and the window's preview not yet) |
| **TABLEEDIT** — `Pick a table cell:`: the cell's text editor, as a double-click opens it (`Enter new text:` on the command line) | TABLEEDIT | Implemented (#44) |
| **TABLESTYLE** — the Table Style window: the drawing's table styles with the current one named; **Set Current**, **New...** (a name, starting from the selected style), **Modify...** (the title, header and data text heights, the cell margin and the lineweight; every table in that style is laid out again) and **Delete** (not Standard, the current style or one a table uses). Without the window, `Enter table style name to make current or [?] <Standard>:`. The current style is kept with the drawing; Annotate > Tables has the Table Style combo | TS | Implemented (#44; colours are set in the drawing only, not yet in the window) |
| **-TABLE** — `Enter number of columns or [Auto] <5>:`, `Enter number of data rows or [Auto] <1>:`, `Specify insertion point or [Style/Width/Height]:` (column width, row height in lines), with a title and a header row above the data rows; a real table entity: rows x columns with stored column widths and row heights, cell text (with the usual `%%`/`\\U+` codes), per-cell alignment and **merged cells**. Borders, grid and text placement are derived from a **TABLESTYLE** (title / header / data text heights, margin, lineweight, colours), so a style edit re-lays out every table. Selectable as a unit (a click in any cell), grips move it and resize columns, native round-trip. **DXF ACAD_TABLE: not written — stated gap**; cell editing and row/column insert are staged | TB | Implemented (issue #22) |
| **Raster IMAGE entity** — an image-definition table (parallel to layers/dimstyles/blocks, deduped) plus placements carrying insertion point, size, rotation and an optional clip rectangle. Payload is an external path relative to the drawing **or** base64 embedded in the `.musa`. Selectable (point-in-quad, clip respected), grip-movable/scalable, bounds-correct, native round-trip, and **plots** (rasterised at output resolution, drawn under the vector geometry). Decoding goes through the core `IImageDecoder` seam so core stays Qt-free | — (no command yet) | Implemented: entity, native + plot, viewport display (textures decoded on the render thread and cached per definition), IMAGEATTACH / IMAGECLIP (rectangular and polygonal) / IMAGEFRAME, 8 MB embed limit, DXF IMAGE / IMAGEDEF both ways (embedded images are written beside the DXF) |
| Per-dimension overrides: resolve override-first-else-style (the Ph12 pattern) in compute_dim_geometry; one undo group; native round-trip; DXF override-vs-style distinction is native-only (stated gap) | — | Implemented (Ph24) |
| PR numeric **geometry editing** — the Geometry group's Start/End, Center/Radius and Position fields edit the entity | — | Implemented (issue #32) |
| LEADER -- `Specify leader start point:`, `Specify next point or [Annotation/Format/Undo] <Annotation>:` until Enter, `Enter first line of annotation text or <options>:` and the next lines (options `[Tolerance/Copy/Block/None/Mtext]`); two points make a leader with its text, more a multileader | LEADER | Implemented (#61; spline leaders, Tolerance, Copy and Block annotations not yet) |
| **DIM** — `Select objects or specify first extension line origin or [Angular/Baseline/Continue/Ordinate/aliGn/Distribute/Layer/Undo]:`, one dimension after another until Enter; the dimension a hovered object would get is previewed at the cursor. A line picked is dimensioned aligned (`Specify dimension line location or second line for angle [Mtext/Text/Angle]:` -- another line picked makes the angle between them), a circle by its diameter, an arc by its radius, two points linearly, each with its command's options. **Angular**, **Baseline**, **Continue**, **Ordinate** run those commands inside DIM; **aliGn** (`Select base dimension:`, `Select dimensions to align:`) puts parallel dimensions on the base's line, concentric angular ones on its arc and ordinate ones of the same datum level with its leader; **Distribute** `[Equal/Offset]` spaces parallel dimensions evenly between the outermost two, or stacks them at a distance from a base; **Layer** sets DIMLAYER; **Undo** takes the last dimension back | DIM | Implemented (#57) |
| DIMLINEAR -- `Specify first extension line origin or <select object>:` (Enter: `Select object to dimension:` a line, a polyline segment, an arc's two ends or a circle's diameter; `O` / `Object` still works), then `Specify dimension line location or [Mtext/Text/Angle/Horizontal/Vertical/Rotated]:`. **Angle** (`Specify angle of dimension text:`, every dimension command) stands the text at its own angle; 0 lays it out as the dimension does; kept in `.musa` and DXF (53). As in AutoCAD the line's position decides: above or below the points it measures across, beside them it measures up, so a vertical dimension of a mostly horizontal pair is a drag away; the preview turns as the cursor goes round. **Horizontal** / **Vertical** fix it, **Rotated** takes an angle (`Specify angle of dimension line <0>:`, a value or two points). **Text** / **Mtext** `Enter dimension text <measured>:` (`<>` stands for the measurement, Enter keeps it). The angle is the dimension's own, kept in `.musa` and DXF (50), so grip edits and DIMCONTINUE / DIMBASELINE follow it | DLI | Implemented |
| DIMALIGNED -- the same two prompts (`<select object>`, `[Mtext/Text/Angle]`) | DAL | Implemented (Angle: the text runs along the line) |
| DIMRADIUS (**select a circle/arc**, or a filleted polyline arc segment → R) | DRA | Implemented |
| DIMDIAMETER (**select a circle/arc** → ⌀ from its own geometry) | DDI | Implemented |
| DIMANGULAR -- `Select arc, circle, line, or <specify vertex>:`: two lines (`Select second line:`), an arc (its own angle), a circle (the picked point, then `Specify second angle endpoint:`) or, with Enter, three points (`Specify angle vertex:`, the two endpoints). Then `Specify dimension arc line location or [Mtext/Text/Angle/Quadrant]:` -- the arc goes where it is clicked and, as in AutoCAD, the angle is the one that point stands in: one of the four two lines make, or for three points and a circle the angle or the rest of the turn (a reflex angle can be dimensioned). Extension lines carry the rays out to the arc. **Quadrant** chooses the angle with a point of its own, the arc then going anywhere. The arc has a grip for its radius; ROTATE, MIRROR and SCALE keep it. DXF: written as before, without the arc's radius (read back as the smaller angle) | DAN | Implemented (Angle: the text stands upright) |
| DIMORDINATE (X/Y datum, leader dogleg) | DOR | Implemented (Mtext/Text/Angle deferred) |
| DIMJOGGED (centre override + jog) | DJO | Implemented (exports to DXF as a radius dimension) |
| DIMARC (arc or polyline arc segment) -- `Specify arc length dimension location, or [Mtext/Text/Angle/Partial/Leader]:`; **Partial** (`Specify first point for arc length dimension:` / `second point`) dimensions the part of the arc between two points; **Leader** draws a radial leader from the text in to the arc (`No leader` takes it off) | DAR | Implemented (exports to DXF as an aligned dimension carrying the arc-length text; the leader is native only) |
| Arrowheads: filled / open / tick / dot (solid filled geometry) | DIMSTYLE | Implemented |
| **DIMSTYLE** -- the Dimension Style Manager, laid out as AutoCAD's: the styles (List: all, or those in use; the current one in bold), a live **Preview of** the selected style -- a sample part with linear, aligned, angular and radius dimensions drawn by the drawing's own dimension code -- and its **Description**; **Set Current**, **New...** (a name, starting from the selected style), **Modify...**, **Rename...** and **Delete** (unused styles only), also on the list's right-click menu. New / Modify open the style on AutoCAD's tabs with the preview beside them, following every change: **Lines** (dimension and extension line colours, lineweight from the standard list, extend beyond, offset from origin), **Symbols and Arrows** (arrowhead with its picture, size, colour -- leaders use them too), **Text** (height, colour, above or centred), **Fit** (what moves outside when the text does not fit; where moved text goes: beside the dimension line, over it with a leader, or over it without one -- DIMTMOVE), **Primary Units** (unit format -- Scientific, Decimal, Engineering, Architectural, Fractional -- with the precision shown as the numbers will read, fraction format, decimal separator, round off, prefix and suffix, measurement scale factor, leading / trailing zero suppression; angles in decimal degrees, degrees minutes seconds, gradians or radians with their own precision), **Alternate Units** (shown in brackets after the value: unit format, precision, multiplier, round off, prefix and suffix). New dimensions, leaders and GD&T frames are drawn in the current style | D / DST / DDIM / DIMSTYLE, Dimension Style buttons | Implemented |
| **-DIMSTYLE** -- `Enter a dimension style option [Save/Restore/STatus/?] <Restore>:`: Restore makes a style current, Save copies the current one under a new name (or redefines one, `redefine it? <N>`) and makes it current, STatus lists the current style's values, ? the styles | -DIMSTYLE | Implemented |
| Dimension variables -- DIMTXT, DIMASZ, DIMBLK (0 filled, 1 tick, 2 open, 3 dot), DIMDEC, DIMDSEP (`.` or `,`), DIMZIN (4 leading, 8 trailing), DIMEXO, DIMEXE, DIMTAD, DIMATFIT (0 auto, 1 inside, 2 outside), DIMTMOVE (0 with the dimension line, 1 with a leader, 2 alone), DIMLWD, DIMCLRD / DIMCLRE / DIMCLRT (ACI, BYLAYER), DIMLUNIT (1 scientific, 2 decimal, 3 engineering, 4 architectural, 5 fractional), DIMFRAC, DIMRND, DIMLFAC, DIMAUNIT (0 degrees, 1 deg/min/sec, 2 grads, 3 radians), DIMADEC (-1: as DIMDEC), DIMALT, DIMALTU, DIMALTD, DIMALTF, DIMALTRND: each sets the current style's value, so a script can set up a drawing's dimensions in one go. (AutoCAD keeps such changes as an override of the style; here they change the style itself.) Kept in `.musa`; DXF writes DIMEXO, DIMEXE, DIMTAD, DIMZIN, DIMDSEP, DIMLWD, the units variables with DIMPOST and DIMAPOST, and `$DIMSTYLE` too | DIMTXT ... | Implemented |
| **QDIM** -- `Select geometry to dimension:`, then `Specify dimension line position, or [Continuous/Staggered/Baseline/Ordinate/Radius/Diameter] <Continuous>:` (the kind is remembered): the ends of lines, arcs and polyline segments, points and circles' centres, dimensioned horizontally when the position is above or below them and vertically beside them -- a chain (Continuous), pairs nested from the outside in (Staggered), each from the first (Baseline; these two stack 1.5 text heights apart), each point's X or Y (Ordinate); Radius and Diameter dimension every circle and arc with the text toward the position. One undo step | QDIM | Implemented (#60; datumPoint, Edit and seTtings not yet) |
| **DIMEDIT** -- `Enter type of dimension editing [Home/New/Rotate/Oblique] <Home>:`, then `Select objects:` (a selection made beforehand is edited at once). **Home** puts the text back where it is laid out and at its own angle; **New** (`Enter dimension text (<> for the measurement) <<>>:`) replaces it, `<>` standing for the measurement; **Rotate** (`Specify angle for dimension text:`) stands it at an angle; **Oblique** (`Enter obliquing angle (press ENTER for none):`) slants a linear or aligned dimension's extension lines to that angle, the measurement unchanged. One undo step | DED | Implemented (#60; the oblique angle is kept in .musa and in DXF, code 52) |
| **DIMTEDIT** -- `Select dimension:`, then `Specify new location for dimension text or [Left/Right/Center/Home/Angle]:`: a point puts the text there as dragging its grip does; **Left** / **Right** slide a linear or aligned dimension's text along its line to the first or second extension line, **Center** to the middle; **Home** and **Angle** as DIMEDIT's Home and Rotate | DIMTED | Implemented (#60; the text does not follow the cursor while you choose) |
| **DIMSPACE** -- `Select base dimension:`, `Select dimensions to space:`, `Enter value or [Auto] <Auto>:`: parallel linear and aligned dimensions stacked out from the base at that spacing -- Auto twice the base's text height -- or, at 0, put on the base's line | DIMSPACE | Implemented (#60) |
| DIMJOGLINE -- `Select dimension to add jog or [Remove]:`, `Specify jog location (or press ENTER):`: a zigzag across a linear or aligned dimension's line where it is placed (a quarter of the way along on Enter), one and a half text heights tall, kept with the drawing; Remove takes it away | DJL | Implemented (#60; native only, not yet in DXF) |
| **DIMINSPECT** -- `Select dimensions:` (or those selected), then the Inspection Dimension window: the **Shape** (Round, Angular or None), a **Label** shown left of the value and an **Inspection rate** right of it (100% to begin with), each with its check box, and **Remove Inspection**. The frame is drawn round the label, the value and the rate with a divider between them; None leaves them side by side unframed. **-DIMINSPECT** asks `Add inspection data or [Remove] <Add>:`, `Enter shape option [Round/Angular/None] <Round>:`, the label and the rate (`.` for none) on the command line. The last settings are offered the next time. One undo step | -- | Implemented (#60; kept in .musa, not yet in DXF) |
| **DIMCENTER** -- `Select arc or circle or [Lines] <mark only>:`: a centre mark of two lines, half the current style's arrow size out from the centre each way (the mark a radius dimension draws); **Lines** adds centre lines from a gap past the mark to beyond the circle, and is remembered for the session | DCE | Implemented (#60; there is no DIMCEN variable -- the mark's size follows the arrow size) |
| **DIMOVERRIDE** -- `Enter dimension variable name to override or [Clear overrides]:`, `Enter new value for dimension variable <2.5000>:` for as many as you like, Enter, then `Select objects:`: the dimensions take those values as their own, over their style's. DIMTXT, DIMASZ, DIMBLK, DIMDEC, DIMTAD, DIMATFIT, DIMCLRD, DIMCLRE and DIMCLRT can be a dimension's own (the colours as ACI 1 to 255); the other variables are the style's. **Clear overrides** drops all of a dimension's own | DOV | Implemented (#60) |
| Object-aware dims capture **def points** at creation (no entity ref) | — | Implemented (deleting the source entity never dangles the dim) |
| Moving dimension text (the text grip) -- as AutoCAD with DIMTMOVE 0: a linear / aligned dimension's line moves with its text, which slides along it between the extension lines or beside them, the line extended under it; radius / diameter text stands where the dimension is placed (outside on a leader with a landing, inside on the line broken around it) and its grip swings the leader round the circle; angular text slides along the arc, which follows it out and grows to reach it. DIMTMOVE 1 / 2: the text moves alone, with or without a leader back to the line | text grip | Implemented |
| Placement preview: the full dimension (with live value) rubber-bands to the cursor, commits on click | — | Implemented (all dim types + DIM; angular arc is fixed by its two lines) |
| Associativity: value recomputed from def points each rebuild | — | Implemented (moving the *referenced* entity does not auto-update) |
| **DIMCONTINUE (DCO)** — `Specify a second extension line origin or [Select/Undo] <Select>:`, the next dimension from the previous one's second extension line, on the same dimension line. **DIMBASELINE (DBA)** — `[Select/Offset/Undo]`, stacked from the first extension line, a spacing further out (**Offset**: `Specify the baseline offset distance <3.7500>:`). Enter or **Select** asks `Select continued dimension:` / `Select base dimension:` -- the extension line nearer the pick is the one gone on from -- and Enter there ends. Linear, aligned, angular (the same vertex and arc, from the previous ray) and ordinate (the next feature on the same datum) dimensions chain; each takes the previous one's style and overrides; each pick is its own undo step and **Undo** comes back to the one gone on from | DCO / DBA | Implemented (#28, #57) |
| MTEXT (multi-line) | MT | Implemented (Ph20; inline per-character formatting is not) |
| MLEADER -- `Specify leader arrowhead location or [leader Landing first/Content first/Options] <Options>:`, the landing, the text beside a horizontal landing; Options: Maxpoints, Content type (Mtext / None), leader lAnding (Yes / No); Annotate ▸ Leaders ▸ Multileader | MLD | Implemented (#61; MLEADERSTYLE, MLEADERCOLLECT, MLEADEREDIT, spline leaders and angle constraints not yet) |
| MLEADERALIGN -- `Select multileaders:`, `Select multileader to align to or [Options]:`, `Specify direction:`: their text (with its landing) moved onto the line through the chosen one's, each keeping its place along it; the arrowheads stay | MLA | Implemented (#61; the Distribute, Parallel and Spacing options not yet) |
| STYLE (text style) | ST | Implemented (#29) |

## View / Navigate

| Command | Alias | Status |
|---|---|---|
| ZOOM -- `Specify corner of window, enter a scale factor (nX or nXP), or [All/Center/Dynamic/Extents/Previous/Scale/Window/Object] <real time>:`: a window (a first corner typed at once starts one), Center with a magnification (`2x`) or a height, Previous (the last ten views ZOOM and PAN left), Scale `nX` against the view or `n` against the limits, All / Extents | Z | Implemented (#72; Dynamic and Object fall back to a window) |
| PAN / -PAN -- the view follows a drag until Esc or Enter; -PAN `Specify base point or displacement:`, `Specify second point:` | P / -P | Implemented (#72) |
| PAN (middle-drag) | P | Implemented (mouse) |
| Zoom about cursor (wheel) | — | Implemented (mouse) |
| Smooth curves at any zoom (adaptive tessellation; re-tessellate on zoom, not pan) | — | Implemented |
| REGEN | RE | Implemented (#33) |
| Named views (VIEW) | V | Implemented (#33) -- Save / Restore / Delete / Window / ? |

## Layers / Properties

| Command | Alias | Status |
|---|---|---|
| Layer Manager (create/delete/rename, on/freeze/lock, colour/linetype/lineweight, set current) | ribbon | Implemented |
| Current-layer dropdown (ribbon) | — | Implemented |
| ByLayer / per-entity override resolution | — | Implemented |
| Move selection to layer | Layer Mgr ▸ Assign | Implemented |
| Set selection colour override | ribbon ▸ Set Colour | Implemented |
| **MATCHPROP / MA** — pick a source object (**or run MA with an object already selected → it becomes the source**, noun-verb), then pick destination object(s) (or **[Settings]**); each destination immediately adopts the source's properties via the Ph22 SetProperty descriptor path (the SAME write path the PR palette uses — no MA-specific entity-write code). **Universal** properties (colour / layer / lineweight / linetype — **ByLayer state preserved**, not the resolved literal) copy across ANY source/target kinds; **family-scoped** (text: height/font/justify/width-factor; dimension: per-dim overrides) copy only within a shared family; non-applicable properties are silently skipped. A **paintbrush cursor** shows while picking; **each matched target is its own undo entry** (undo in reverse). **Settings** (type `S`) opens a dark modal listing the categories (all on by default; persisted in QSettings for the session) | MA / PAINTER | Implemented |
| Off / Frozen layers skip rendering | — | Implemented |
| Locked layers (drawn, not selectable/modifiable) | — | Implemented |
| Linetype property (Continuous/Dashed/Center/Hidden) | — | Implemented (model + round-trip + **dashed rendering**, Ph23) |
| **Linetype pattern rendering** (dashes drawn on lines, polylines, and curves by arc-length; ByLayer + override) | — | Implemented (Ph23) |
| **LTSCALE** (drawing linetype scale, **per document**; prompts `<current>`, rejects ≤0; re-dashes live; native + DXF `$LTSCALE` round-trip) | LTSCALE / LTS | Implemented (Ph23) |
| **CELTSCALE** (per-entity linetype scale; PR "Linetype scale" field on line/circle/arc/polyline; MATCHPROP-matchable; native + DXF code 48 round-trip). Effective dash scale = **LTSCALE × CELTSCALE** | PR ▸ Linetype scale | Implemented |
| **Dynamic Input (canvas-only by default)** — all command input is drawn ON the canvas at the crosshair: command entry + autocomplete, mid-command sub-prompts, and on-geometry dimension fields. **F12 toggles** canvas-only ⇄ the classic bottom command-line bar (F12 ON = canvas, bar hidden; F12 OFF = bar visible, no canvas DYN). State persists; the bar always returns on F12 / the status DYN toggle (no-stuck fallback) | F12 / status DYN toggle | Implemented (Ph25–27; state persisted) |
| **DYN canvas command entry** — start typing on an idle canvas and a command-entry box appears at the cursor with the registry autocomplete dropdown (Down/Up/Tab select, Enter runs, Esc clears); routed by the app-wide event filter; bounded glyph batches regardless of suggestion count | — | Implemented (Ph27) |
| **DYN canvas sub-prompts** — any mid-command value/keyword step (FILLET radius, CHAMFER distances, RECTANGLE Dimensions length/width, Area, Rotation, option keywords) renders as an at-cursor prompt cell; type → Enter commits/advances, Esc cancels. Same primitive as entry + dimension fields | — | Implemented (Ph27) |
| DYN live dimensional input: type an exact length/angle (line), radius (circle), width/height (rectangle) during the rubber-band; Tab between fields | — | Implemented (Ph25; honors ORTHO/POLAR/snap) |
| **DYN on-geometry value fields** — value boxes drawn ON the canvas (in the GL viewport overlay, NOT OS windows) anchored to the rubber-band geometry, so they are always glued to it and cannot drift on multi-monitor (length under one edge, width by the other for RECTANGLE; length + angle for LINE and PLINE's next point; radius for CIRCLE), nudged just outside the edge so they never overlap. Type WITHOUT a click (the viewport captures dimension keystrokes); **Tab/Shift-Tab** switch fields; **Enter/Space** commit; **Esc** cancels; the mouse still drives the rubber-band. A typed value locks that dimension while the cursor drives the other(s) | F12 | Implemented (RECTANGLE/LINE/PLINE/CIRCLE; other commands staged) |
| **DYN command-control keys (Esc / Enter / Space)** — with DYN on, these always reach the command (never swallowed by the on-canvas fields). **Esc** always cancels the active command — even with a half-typed value — clearing the rubber-band and fields. **Enter** / **Space** are AutoCAD's two-step: a pending typed value commits (keep drawing), otherwise the step ends (Enter at a LINE next-point prompt ends LINE). Tab/Shift-Tab still cycle fields. One carve-out in the app-wide event filter; F12-OFF (classic bar) unchanged | Esc / Enter / Space | Implemented |
| Single input surface per step: every step has exactly ONE place to type — the on-canvas entry (idle), sub-prompt cell (mid-command scalar/keyword), or dimension fields (rubber-band). The legacy cursor box is retired. State lives in the `CommandProcessor` (prompt + history), so hiding the bottom bar loses nothing; everything submits through the same pipeline (`compose_dyn_submit` / `submit_line`) | — | Implemented |
| **DYN autocomplete**: the Ph6 command-suggestion dropdown anchored at the cursor entry box (one suggestion source: the registry) | — | Implemented (Ph26–27) |
| Draw/transform ribbon buttons **start the interactive command** (pick on screen, like typing — never a fixed-position dialog): Line/Circle/Arc/Rectangle/Rotate/Scale | ribbon | Implemented (AutoCAD model) |
| **Starting a new command cancels the one in progress** — clicking a ribbon command (or otherwise dispatching a new command) while one is active cleanly cancels the current command first (its rubber-band/preview is dropped) and starts the new one; the **selection is preserved**. One dispatch site (`CommandProcessor::start_command`) | ribbon / dispatch | Implemented |
| **CIRCLE radius/[Diameter] option** — type `D` at the radius prompt (command line or DYN) to enter a diameter instead | C → D | Implemented (Ph26) |
| Parametric **multi-parameter dialog** (collect values → submit existing Command): ARRAY | ribbon | Implemented (Ph11) |
| POLYGON command | POL | Implemented (#23; the prompts, no dialog) |
| **Import DWG** — runs an external converter (DWG→DXF) off-thread, then the existing DXF importer (fail-safe); writes a `<file>.dwg.import.log` gap catalog | ribbon / DWGIN | Implemented (Ph27; needs an installed converter — ODA File Converter or LibreDWG) |
| **Export DWG** — existing DXF export, then the external converter (DXF→DWG, default ACAD2018) | ribbon / DWGOUT | Implemented (Ph27; two-stage lossy, see ARCHITECTURE) |
| **DWG Setup** dialog — **Download ODA File Converter** (fetched from opendesign.com into the app's data directory after you accept its terms; "Remove downloaded" deletes it), or detect / Browse / auto-detect a converter you installed; saves the path setting (offered, with the download, when a DWG finds no converter) | ribbon "DWG Setup" | Implemented |
| DWG converter path (configurable) | `io/dwg_converter_path` setting / DWG Setup dialog | Implemented (Ph27; auto-detects ODA/LibreDWG on PATH otherwise) |
| Per-entity linetype scale (CELTSCALE) | PR "Linetype scale" | Implemented (native v12, DXF 48) |
| Lineweight property (hundredths-mm) | LWT toggle | Implemented (model, round-trip, on-screen weight with LWDISPLAY) |
| LAYER command-line alias / PROPERTIES palette / MATCHPROP | LA / PR / MA | Implemented |

## Blocks / Reference

Block **definitions** + **INSERT** references (transform-at-snapshot, not exploded). This
phase covered **import, display, and selection**; authoring followed under issue #25.

| Command | Alias | Status |
|---|---|---|
| Block import (BLOCKS + INSERT, nested, scale/rotation) from DWG/DXF | File ▸ Import | Implemented (Ph28) |
| Display block instances (definition × transform, resolved at snapshot; per-instance colour/layer; batched — N instances add no draw calls) | — | Implemented (Ph28) |
| Select / hover / window-crossing a block as one object; move / erase / copy the instance (not the definition); insertion-point grip; INS osnap | click / grips / Modify | Implemented (Ph28) |
| BLOCK / WBLOCK — define a block from selected geometry | B / W | Implemented (#25) |
| REFEDIT — edit a definition; all instances update | REFEDIT / REFSET / REFCLOSE | Implemented (#25) |
| EXPLODE — instance → its geometry | X | Implemented (#25) |
| ATTDEF / ATTRIB — block attribute text | ATT | Implemented (issue #25: ATTDEF, INSERT value prompts, ATTDISP, ATTEDIT; DXF ATTDEF and INSERT+ATTRIB) |
| XREF | XR | Implemented (issue #25): attach / detach / reload / ?, re-read on open, native v32, DXF block flag 4 with the path; a drawing in other units (INSUNITS) comes in scaled to this one's (#81) |

## File / Plot

| Command | Alias | Status |
|---|---|---|
| Native format **v15** (adds dimension text decoration) | — | Implemented (v1–v14 load) |
| Native format v5 (adds polyline arc bulges) | — | Implemented (v1–v4 load) |
| DXF LWPOLYLINE bulges (code 42, read/write) | — | Implemented (LibreCAD-verified) |
| DXF TEXT + DIMENSION (all subtypes) + LEADER + DIMSTYLE table | — | Implemented (leader label imports as separate TEXT) |
| LWDISPLAY (lineweight display on/off) | LWT ribbon toggle | Implemented |
| Lineweight display: DPI-anchored `px = mm × DPI/25.4`, zoom-independent, Default = 1px hairline (AutoCAD-accurate) | — | Implemented |
| NEW — opens a new untitled drawing in a **new tab** (existing tabs untouched) | Ctrl+N / "+" tab button | Implemented |
| OPEN (native .musa) — **always opens into a new tab**; unsaved work in the current tab is never overwritten | Ctrl+O | Implemented |
| SAVE / SAVE AS — operate on the **active** document (its own path) | Ctrl+S / Ctrl+Shift+S | Implemented |
| **Multiple documents as tabs** — N drawings open at once; each tab shows its name + a `*` dirty marker. ONE engine swaps the active document on switch; ONE viewport renders the active document's snapshot | tab strip | Implemented (Phase A) |
| **Switch tab** — click a tab, or cycle with Ctrl+Tab / Ctrl+Shift+Tab. Each tab restores its own camera (zoom/pan), selection, and undo/redo history | Ctrl+Tab / Ctrl+Shift+Tab | Implemented |
| **Close tab** — the × on each tab, or Ctrl+W. A dirty tab prompts Save / Discard / Cancel; closing the last tab leaves one empty "DrawingN" (never zero tabs) | × / Ctrl+W | Implemented |
| **Undo/redo is per document** — Ctrl+Z on tab A rewinds tab A's last op even after edits in tab B | Ctrl+Z / Ctrl+Y | Implemented |
| **Quit guard** — closing the window prompts to save every dirty document (Cancel aborts the quit) | — | Implemented |
| **COPYCLIP / CUTCLIP** — copy / cut the selection (asked for when nothing is selected). The objects go to the system clipboard too: as Musa CAD objects another Musa CAD window pastes, and as a picture on white (white lines drawn black, as on paper) for documents, mail and image editors | Ctrl+C / Ctrl+X | Implemented |
| **COPYBASE / CUTBASE** — the same with a base point first: the point a paste puts on the insertion point | COPYBASE / CUTBASE | Implemented |
| **PASTECLIP** — `Specify insertion point:` with the copied objects following the cursor; layers, dimension styles, blocks, fonts and images are matched by name in the target drawing (created where missing); one undo step. From another program: an **image** is embedded with its lower-left corner at the point (a pixel to a drawing unit), **text** becomes a multiline text at the current text style's height | Ctrl+V / PASTECLIP | Implemented |
| **PASTEORIG** — the copied objects at their own coordinates, nothing asked (into another tab or another window) | PASTEORIG | Implemented |
| **PASTEBLOCK** — the copied objects as one block (named `A$C` and eight hex digits) inserted at `Specify insertion point:`, one undo step | PASTEBLOCK | Implemented (#55) |
| **Tab-to-tab drag** — drag a selection onto another document's tab to transfer it there (copy → switch → paste, original coordinates) | drag to tab | Implemented (Phase B) |
| DXF export (R2000 / AC1015; LAYER table + ByLayer colour 256) | File ▸ Export DXF | Implemented |
| DXF import (LINE/LWPOLYLINE/CIRCLE/ARC/POINT/TEXT/MTEXT/DIMENSION/LEADER; BLOCK defs + INSERT refs; reads the LAYER table + ACI colours) | File ▸ Import DXF | Implemented |
| DXF import (SPLINE / legacy POLYLINE) | — | Implemented (issue #31): uniform-knot and fit-point splines come in as splines, other knot vectors as polylines; the R12-era POLYLINE/VERTEX/SEQEND form comes in as a polyline (bulges, closed flag, inside blocks too), while 3D polylines and meshes are reported as skipped |
| Dirty tracking (modified `*` in title, prompt before discard) | — | Implemented |
| PLOT / PRINT (PDF + installed printers; paper/orientation/area Display·Extents·Window/scale fit·ratio/centre·offset/lineweights/CTB None·Mono·Grayscale/copies; window-pick; print-preview; off-thread; vector output) | Ctrl+P / PLOT / PRINT | Implemented (Phase 30) |
| Saved page setups (named, persisted in the drawing; recall in the PLOT dialog) | PLOT ▸ Page setup | Implemented (Phase 30) |
| **LAYER** (`LA`) opens the Layer Properties Manager; **-LAYER** (`-LA`) -- `Enter an option [?/Make/Set/New/Rename/ON/OFF/Color/Ltype/LWeight/Plot/Freeze/Thaw/LOck/Unlock/Description]:` until Enter: ? lists the layers matching a pattern with their state, colour, linetype and lineweight; Make adds a layer and makes it current; Set makes one current (or the layer of an object picked); New adds a comma list; Rename (not layer 0, not to a name in use); ON / OFF / Freeze / Thaw / LOck / Unlock take a comma list with `*` and `?` wild cards (Enter: the current layer; the current layer is not frozen); Color (a number, a name or red,green,blue), Ltype (Continuous, Dashed, Center, Hidden) and LWeight (mm) ask the value, then the layers; Plot (`Enter a plotting preference [Plot/No plot] <Plot>:`) and Description (`Enter layer description:`) likewise. The Layer Properties Manager has Plot and Description columns; a layer not plotted is left off every plot. Both are kept in the drawing and in DXF (group 290, and the description where AutoCAD keeps it) | LA / -LA | Implemented (#69; TRansparency, MATerial, stAte, rEconcile and Xref not yet) |
| **Layer tools** — LAYOFF / LAYFRZ (`Select an object on the layer to be turned off:` / `... frozen:`, one object after another, `[Undo]`; the current layer cannot be frozen), LAYLCK / LAYULK (`Select an object on the layer to be locked:` / `unlocked:`), LAYMCUR (`Select object whose layer will become current:`), LAYCUR (`Select objects to be changed to the current layer:`), LAYISO (`Select objects on the layer(s) to be isolated:`; every other layer off, the current layer moved to an isolated one) and LAYUNISO, LAYON, LAYTHW; LAYMCH (`Select objects to be changed:`, `Select object on destination layer or [Name]:`) | LAYOFF / LAYFRZ / LAYLCK / LAYULK / LAYMCUR / LAYCUR / LAYISO / LAYUNISO / LAYON / LAYTHW; Home ▸ Layers | Implemented (Settings not yet: viewports and block nesting levels) |
| **LAYERP** -- the layer settings back as they were before the last change (on, frozen, locked, colour, linetype, lineweight, plot, the current layer; new and renamed layers stay), one change at a time; **LAYERPMODE** `Enter LAYERP mode [ON/OFF]:` | LAYERP / LAYERPMODE; Home ▸ Layers ▸ Previous | Implemented (#69) |
| **LAYERSTATE** (`LAS`, -LAYERSTATE) -- `Enter an option [?/Save/Restore/Delete/Name]:`: named layer states with every layer's settings and the current layer, kept with the drawing (native v40) | LAYERSTATE / LAS | Implemented (#69; the Layer States Manager window, Import / Export and DXF not yet) |
| **LAYWALK** -- the LayerWalk window: only the layers selected in its list are shown (Ctrl and Shift for several); Restore on exit puts every layer back as it was | LAYWALK | Implemented (#55; filters and Purge in the window not yet) |
| **Crash reports** — if Musa CAD stops unexpectedly it writes a report (version, system, graphics driver, the drawing's file name, the last 96 command-line lines, the call stack); the next start shows it with **Save Report As…**, **Copy** and **Open Folder** to attach to an issue. **Save Bug Report…** in the application menu writes the same report on demand | application menu ▸ Save Bug Report… | Implemented |
| **OPTIONS** — Updates: check automatically (once a day; on by default for packaged installs, off for source builds) and Check Now; Display: the performance overlay (frame rate / frame time in the drawing area, frame rate and build date in the title; off by default). Persisted per user | OP / application menu ▸ Options | Implemented |
| **Update check** — asks Flathub (the Flatpak) or the GitHub release (AppImage, Windows installer, macOS) for the latest version; a newer one shows as a status-bar note that opens the update step for this install (the `flatpak update` command, or the file to download); Skip This Version; `MUSACAD_NO_UPDATE_CHECK=1` turns it off for packagers and CI | application menu ▸ Check for Updates | Implemented |
| **Command line** — `musacad <drawing>` opens a file (via the existing OpenDocumentCommand); `musacad --check <drawing>` validates it and exits non-zero on a parse error; `--help` / `--version`. Parsed before any Qt object exists, so it needs no display | shell | Implemented (issue #11) |
| **Headless plot** — `musacad --plot <drawing> <out.pdf> [--paper A4] [--portrait\|--landscape] [--scale 1:5\|--fit] [--window x0,y0,x1,y1\|--extents] [--monochrome]`. Same loaders, snapshot builder, paper table, tessellation rule and PDF writer as the GUI's PLOT; offscreen Qt platform forced, so it runs from cron/CI/ssh. Exit codes 0/1/2/3 | shell | Implemented (issue #11) |

## Object snap (OSNAP)

| Mode | Status |
|---|---|
| Endpoint | Implemented |
| Midpoint | Implemented |
| Center | Implemented |
| Intersection | Implemented |
| Nearest | Implemented |
| Quadrant (circle/arc N/E/S/W) | Implemented |
| Node (Point entities) | Implemented |
| Perpendicular (deferred; line + circle/arc) | Implemented |
| Tangent (deferred; circle/arc) | Implemented |
| Geometric Center (`GCEN`): the centroid of a closed polyline | Implemented |
| Apparent intersection / Insertion / Parallel | Implemented (issue #32) |
| Extension (`EXT`): pass the cursor over the end of a line, an arc or an open polyline to acquire it (a small cross marks it), then move along the line carried on past that end, or round the arc's circle; a dashed path runs from the end to the snap point, and two acquired lines give their crossing | Implemented (issue #66) |

A polyline's arc segments snap at their true midpoint and at their centre.

OSNAP precedence (highest→lowest, within the aperture): Endpoint, Midpoint,
Center, Node, Quadrant, Intersection, Perpendicular, Tangent, Geometric Center,
Insertion, Apparent intersection, Parallel, Extension, Nearest.
Each type is individually toggleable via the running-osnap mask (OSNAP status-bar
button dropdown, the settings dialog, `-OSNAP`, `OSMODE`). A fresh session runs
AutoCAD's default set: Endpoint, Center, Intersection and Extension.

### One pick only

At any prompt that asks for a point, a snap mode typed instead of the point holds for
that one pick, whatever the running snaps are and even with OSNAP off: `END`, `MID`,
`CEN`, `GCEN`, `NOD`, `QUA`, `INT`, `EXT`, `INS`, `PER`, `TAN`, `NEA`, `APP`, `PAR`
(several may be given, `END,MID`), and `NON` for no snap at all. The prompt shows it
(`Specify next point or [Undo]: _end of`). A pick with nothing of the kind under the
cursor is refused (`No Endpoint found for specified point.`) and the prompt asks again.
Shift + right-click opens the same list as a menu.

| Typed | What it gives |
|---|---|
| `FROM` | `Base point:` then `<Offset>:` -- a point at an offset (`@dx,dy`, `@dist<angle`) from a base point, the base usually taken with a snap |
| `M2P` / `MTP` | `First point of mid:` / `Second point of mid:` -- the point half way between two points |
| `TT` | `Specify temporary OTRACK point:` -- the cursor then locks onto the horizontal and the vertical through that point (with ORTHO, where the ortho direction crosses them) until the next point is given |
| `TK` | `First tracking point:` then `Next point (Press ENTER to end tracking):` -- a chain of moves, each along one axis; Enter gives the point reached |

## Selection & live preview

AutoCAD's selection conventions are the base every edit command stands on (issue #46):

| Feature | Status |
|---|---|
| **Verb-noun**: MOVE, COPY, MIRROR, ROTATE, SCALE, ALIGN, ARRAY, ERASE, EXPLODE, STRETCH, MATCHPROP's destinations, GROUP, ISOLATEOBJECTS and SELECT prompt `Select objects:` when nothing is selected and gather picks, windows, crossings and lassos until Enter or right-click; a pre-selection (noun-verb) skips the prompt; Enter with nothing chosen ends the command quietly | Implemented |
| The keywords at every `Select objects:` prompt: **Window, Crossing, BOX** (by direction), **ALL, Fence, WPolygon, CPolygon, Group, Last, Previous, Add, Remove** (`Remove objects:`), **Multiple, Undo, AUto, SIngle**; "N found" / "N found, M total" / "N found, M removed, K total" as AutoCAD echoes them | Implemented |
| Single-click pick (pick-box, PICKBOX px); **Shift + click removes** a selected object (PICKADD 2; PICKADD 0 = a click replaces) | Implemented |
| Window select (left→right, enclosed, blue tint); crossing select (right→left, touched, green tint); **click-click boxes** from an empty click (PICKAUTO) | Implemented |
| **Lasso** by press-and-drag (PICKDRAG 2): leftwards a crossing, rightwards a window; **Space** cycles window / crossing / fence while dragging | Implemented |
| **SELECTIONPREVIEW**: the objects a box or lasso would select highlight while it is dragged (1 idle, 2 at a prompt, 3 both) | Implemented |
| **Selection cycling** (SELECTIONCYCLING, Ctrl+W): a pick over several objects offers the list at the cursor | Implemented (the badge is not drawn) |
| Ctrl+A selects all; Esc clears; **HIGHLIGHT** 0 turns the selection highlight off; PICKFIRST 0 makes idle clicks inert | Implemented |
| **SELECT, SELECTSIMILAR** (SELECTSIMILARMODE), **QSELECT** and **FILTER** (the Quick Select dialog; FILTER adds several conditions), the idle **right-click menu** (Repeat, Recent Input, Clipboard, Isolate, Erase, Select Similar, Quick Select, Deselect All, Undo / Redo, Properties) | Implemented |
| Selected-entity highlight (orange) | Implemented |
| Rollover (hover) highlight (light blue) | Implemented |
| Live cursor preview (Line/Circle/Rect/PLine/Arc) | Implemented |
| Move/Mirror ghost preview | Implemented |
| Ortho/Polar/grid-snap honored by preview | Implemented |

## Grip editing (direct manipulation)

| Feature | Status |
|---|---|
| Grips (blue squares) show on a selected entity; grabbed/hovered grip goes hot (red) | Implemented |
| Drag a grip → live preview; release commits as one undo step; Esc cancels (entity unchanged) | Implemented |
| **Hot grips** -- click a grip (rather than drag it): `** STRETCH ** Specify stretch point or [Base point/Copy/Undo/eXit]:`; Enter or Space cycles STRETCH, MOVE, ROTATE, SCALE and MIRROR about the grip (or a Base point), on the selection; Copy leaves the originals and places copies until eXit | Implemented (#54; Copy with STRETCH, Reference, several hot grips with Shift and the hover menus not yet) |
| Zero store mutation / op-log churn during the drag (transient preview) | Implemented |
| ORTHO/POLAR/OSNAP honored on the dragged grip | Implemented |
| Line (2 endpoints + midpoint-move) | Implemented |
| Circle (centre-move + 4 quadrant-radius) | Implemented |
| Arc (centre-move + 2 endpoints + mid-radius) | Implemented |
| Polyline / Rectangle (per-vertex move) | Implemented |
| Hatch (per-boundary-vertex move → reshape the filled region) | Implemented |
| Text (insertion-point move) | Implemented |
| Dimension: full grip set — both ext-line origins, both dim-line ends, offset midpoint (grab anywhere, place anywhere) | Implemented (Linear/Aligned) |
| Dimension: **dim-line offset** (move the dim line, value unchanged) | Implemented |
| Dimension: def-point drag (re-measures, live value) | Implemented (Linear/Aligned/Radius/Diameter/Angular) |
| HiDPI: lineweights + grip/snap/crosshair sizes are the same physical size on 1×/2× displays (DPR-corrected) | Implemented |
| Dimension: independent text-reposition grip (all five types) + connector leader + PR "home text" | Implemented (issue #21) |
| Add/remove polyline vertex via grips | Implemented (issue #32): a midpoint grip per segment (drag moves a straight segment, reshapes an arc); right-click a grip for Add Vertex / Remove Vertex / Convert to Arc / Convert to Line |

## Ribbon (responsive + contextual)

The ribbon follows AutoCAD's Drafting & Annotation workspace: the same tabs, panels,
groupings and shapes, over Musa CAD's commands. A tool Musa CAD does not have yet stands
in AutoCAD's place as a disabled button (its issue is linked from the audit issues).

| Tab | Panels (large tools; stacked small tools; slide-out behind the title's ▾) |
|---|---|
| **Home** | **Draw**: Line, Polyline, Circle ▾ (Center-Radius, Center-Diameter, 2-Point, 3-Point, Tan Tan Radius, Tan Tan Tan), Arc ▾ (the eleven methods); Rectangle ▾ (Polygon), Ellipse ▾ (Center, Axis End, Elliptical Arc), Hatch ▾ (Gradient); slide-out: Revision Cloud, Wipeout, Donut, Spline ▾ (Fit / CV), Point ▾ (Multiple Points, Divide, Measure), Polygon, Construction Line, Ray, Region. **Modify**: Move, Copy, Stretch / Rotate, Mirror, Scale / Trim ▾ (Extend), Fillet ▾ (Chamfer), Array ▾ (Rectangular, Path, Polar) / Erase, Explode, Offset; slide-out: Lengthen, Edit Polyline, Break, Break at Point, Join, Align, Match Properties, Blend Curves, Delete Duplicate Objects. **Annotation**: Text ▾ (Multiline, Single Line), Dimension; Linear ▾ (Aligned, Angular, Arc Length, Radius, Diameter, Jogged, Ordinate), Leader ▾, Table; slide-out: Dimension Style, Text Style, Edit Text, Continue, Baseline, Tolerance; launcher: Dimension Style. **Layers**: Layer Properties, the current layer, Off / Isolate / Freeze / Make Current. **Block**: Insert; Create ▾ (Write Block), Edit, Edit Attributes ▾. **Properties**: Match Properties; the colour, linetype and lineweight controls (the selection's, or the current entity properties for new objects); slide-out: Linetype Scale, List, Lineweight Display; launcher: the Properties palette. **Groups**: Group; Ungroup, Group Edit, Group Selection On/Off. **Utilities**: Measure ▾ (Distance, Area, ID Point, List); Quick Select, Quick Calculator, ID Point. **Clipboard**: Paste; Cut, Copy Clip, Copy with Base Point |
| **Insert** | Block; Block Definition; Reference (Attach ▾ Xref / Image, Clip, Frames ▾, External References ▾); Import ▾; Data (Field, Table, Update Fields, Data Link) |
| **Annotate** | Text (Multiline Text ▾, Text Style, Edit Text, Find Text); Dimensions (Dimension, Linear ▾, Continue ▾, Dimension Style; slide-out Tolerance, Datum, Center Mark); Leaders; Tables; Markup (Revision Cloud, Wipeout) |
| **Parametric** | Geometric, Dimensional, Manage -- AutoCAD's constraint tools, all placeholders (#70) |
| **View** | Navigate 2D (Zoom ▾: Extents, All, In, Out, Scale; Pan); Named Views ▾ (Save, Restore, Delete, List; New View); Model Viewports (Viewport Configuration ▾, Named, Join); Palettes (Properties, Layer Properties, Tool Palettes) |
| **Manage** | Cleanup (Purge, Audit); Customization (User Interface, Tool Palettes) |
| **Output** | Plot (Plot, Batch Plot, Preview, Page Setup Manager); Export ▾ (DXF, DWG, PDF), DWG Setup |

The application menu on the Musa mark carries New, Open, Save, Save As, Import, Export,
Plot, DWG Setup, About and Exit; the Quick Access Toolbar beside it has New, Open, Save,
Save As, Undo, Redo and Plot. A large split button (Circle ▾) runs its command from the
icon and opens its menu from the label; a small one (Rotate ▾) opens the menu from the
chevron at its right. A method entry such as "Circle ▸ 2-Point" runs the command and
feeds it the option (AutoCAD's ribbon macros). Home's stacked tools are bare icons with
the names in their tooltips, as in AutoCAD; panels collapse in three stages as the window
narrows (labels off the small tools, then a fly-out per panel), and the Musa palette
(navy, orange, teal) marks the active tab, hover and pressed states.

The ribbon is registry-driven (icons + tooltips from each command's registration) and
**responsive**: as the window narrows, panels degrade FULL → COMPACT (secondary buttons
become icon-only) → COLLAPSED (the panel folds to one fly-out button that pops out the full
panel inline), lowest-priority panels first; a horizontal scroll bar is the final fallback.

**Contextual tabs** appear automatically when the whole selection matches an entity family,
and disappear (returning to the last fixed tab) when it doesn't. Mixed selections show none.

| Contextual tab | Appears when | Controls | Status |
|---|---|---|---|
| **Hatch Editor** (green accent) | all selected objects are hatches | Pattern picker, Solid-fill toggle, Scale, Angle, Re-hatch | Implemented |
| **Text Editor** (blue accent) | all selected objects are the Text family (TEXT/MTEXT/leaders) | Font, Height, Edit Text | Implemented |
| **Block Editor** (amber accent) | a single block reference (INSERT) is selected | Edit Reference (REFEDIT), Edit Attributes (EATTEDIT), Manage Attributes (BATTMAN) | Present |

## Status-bar modes & keys

| Mode | Key | Status |
|---|---|---|
| Object Snap | F3 | Implemented |
| Grid display | F7 | Implemented |
| Ortho | F8 | Implemented; **ORTHO** `Enter mode [ON/OFF] <OFF>:` at the command line |
| Snap (grid snap) | F9 | Implemented; **SNAP** `Specify snap spacing or [ON/OFF/Aspect/Legacy/Style/Type] <10.0000>:` (a spacing turns snap on; **Aspect** asks the horizontal and the vertical spacing; **Type** Polar / Grid; Style Isometric and Legacy are not there yet and say so). **GRID** `Specify grid spacing(X) or [ON/OFF/Snap/Major/aDaptive/Limits/Follow/Aspect] <0.0000>:` -- a spacing (or `2x`, twice the snap spacing; 0, the grid following the zoom as it does by default) turns the grid on at that spacing; **Snap** takes the snap spacing; **Major** a major line every so many; **aDaptive** fewer lines zoomed out and, with subdivision, more zoomed in; **Limits** No stops the grid at the drawing limits; **Aspect** a horizontal and a vertical spacing. Follow is accepted (there is no dynamic UCS). **LIMITS** -- `Specify lower left corner or [ON/OFF] <0.0000,0.0000>:`, `Specify upper right corner <420.0000,297.0000>:`; with checking on a point outside is refused at every prompt (`**Outside limits`); kept in the drawing and in DXF (`$LIMMIN`, `$LIMMAX`, `$LIMCHECK`) |
| Polar tracking | F10 | Implemented -- alignment paths at the polar angles from the last point; the cursor locks onto one within the aperture of it (a dashed path and a tooltip, `Polar: 12.3456 < 45°`). Increment angles from the POLAR dropdown or `POLARANG` (90 to begin with), additional angles (`POLARADDANG`, used with `POLARMODE` 4), angles relative to the last segment (`POLARMODE` 1), PolarSnap (`SNAPTYPE` 1 with SNAP on: steps of `POLARDIST` along the path) |
| Object snap tracking | F11 | Implemented -- rest the cursor on an object snap point to acquire it (a small cross; rest on it again to let it go; seven at most), then track along the horizontal and vertical through it (every polar angle with `POLARMODE` 2), onto the crossing of two paths or of a path and a polar path (`Endpoint: < 90°, Polar: < 0°`). `AUTOSNAP` bits 8 and 16 switch the two tracking modes |
| Temporary override keys, held at a point prompt | Shift … | Implemented -- Shift: ORTHO the other way; Shift + A: OSNAP; Shift + X: POLAR; Shift + Q: OTRACK; Shift + D (or L): no snapping or tracking; Shift + E (or P) / V (or M) / C: Endpoint / Midpoint / Center alone. `TEMPOVERRIDES` 0 switches them off |
| Undo / Redo | Ctrl+Z / Ctrl+Y | Implemented |
| Delete selection | Delete / Backspace | Implemented |
| Clear selection | Esc | Implemented |

## Coordinate entry

| Form | Example | Status |
|---|---|---|
| Absolute | `10,20` | Implemented |
| Relative | `@30,0` | Implemented |
| Polar relative | `@30<45` | Implemented |

## Command-line UX

| Feature | Status |
|---|---|
| Table-driven aliases | Implemented |
| Autocomplete dropdown (registry-driven) | Implemented |
| Up/Down history, ENTER repeats last | Implemented |
| Honest command results (engine echoes what actually happened) | Implemented |
| Parametric input dialog (ARRAY: rectangular + polar) | Implemented |
| Input dialogs for Rotate/Scale + live ghost preview | Implemented (#32) |
| Dynamic input tooltips at cursor | Implemented (full canvas DYN: command entry + autocomplete, mid-command sub-prompts, on-geometry dimension fields for RECTANGLE/LINE/CIRCLE; F12 toggles canvas-only ⇄ classic bottom bar) |
