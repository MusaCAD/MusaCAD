<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Musa CAD — command line

The shipped `musacad` binary has a documented command line, so a drawing can be
opened, validated or plotted from a script without a GUI session and a human.

```
musacad [<drawing>]              open a drawing in the GUI
musacad --check <drawing>        parse a drawing, report errors, exit
musacad --plot <drawing> <out.pdf> [plot options]
musacad --help
musacad --version
```

`<drawing>` is a `.musa` file, or a `.dxf` file (loaded through the DXF importer —
the extension chooses the loader).

## Exit codes

Scripts depend on these, so they are part of the interface:

| Code | Meaning |
|---|---|
| `0` | success |
| `1` | usage error (bad command line) |
| `2` | the drawing could not be read or parsed |
| `3` | the output could not be written |

## Opening a drawing

```sh
musacad part.musa
```

The file argument rides the **existing** `OpenDocumentCommand` — the same
geometry-thread path `File ▸ Open` uses, into a new tab. There is no second load
path, so a drawing that opens from the CLI behaves identically to one opened from
the dialog (including the fail-safe rule: on a parse error the store is left
untouched and the error is reported).

## Validating a drawing

```sh
musacad --check part.musa && echo "ok"
```

`--check` runs the same core loader (`io::load_native` / `io::load_dxf`) the engine
runs, prints what it read to stdout on success, and prints the parser's message to
**stderr** with exit code `2` on failure. It constructs **no Qt application object**,
so it works with no `DISPLAY`/`WAYLAND_DISPLAY` and no GPU:

```sh
$ musacad --check broken.musa
musacad: broken.musa: LINE record malformed (line 2).
$ echo $?
2
```

## Plotting headlessly

```sh
musacad --plot drawing.musa out.pdf \
        --paper A4 --portrait \
        --scale 1:5 --window 0,0,420,297
```

| Option | Meaning |
|---|---|
| `--paper <name>` | `A4` `A3` `A2` `A1` `A0` `Letter` `Tabloid` (or the full `ISO A4` / `ANSI A (Letter)` names). Default `A4`. |
| `--landscape` / `--portrait` | Sheet orientation. Default landscape. |
| `--extents` | Plot the drawing's extents. **Default.** |
| `--window x0,y0,x1,y1` | Plot an explicit world rectangle. Corners may be given in any order. |
| `--fit` | Scale the area to fill the sheet. **Default.** |
| `--scale <n:m>` | Plot `n` mm of paper per `m` drawing units (e.g. `1:5`). Implies `--fit` off. |
| `--monochrome` | Plot everything black (the built-in monochrome CTB style). |

**No display is required, and none is used.** Plotting is the QPainter/`QPdfWriter`
route, so `--plot` constructs a `QGuiApplication` (never `QApplication`), creates no
GL context and never touches the renderer. It forces the **offscreen** Qt platform
plugin unless you pass `-platform` explicitly — deliberately overriding any inherited
`QT_QPA_PLATFORM`, because a desktop session exports something like `wayland;xcb` and a
plot launched from cron, CI or ssh with that in its environment would otherwise abort
with *"no Qt platform plugin could be initialized"*. That is precisely the unattended
use this option exists for.

### It is the same plot path the GUI uses

`--plot` is not a second renderer. It shares, with `PLOT`/`Ctrl+P` and with
`tools/plot_check`:

* the **loaders** (`io::load_native` / `io::load_dxf`),
* the **snapshot builder** (`core::build_render_snapshot`) — so linetype dashing,
  `LTSCALE × CELTSCALE`, hatch fills, lineweights and text layout are identical,
* the **paper table** (`ui::standard_papers`),
* the **tessellation rule** (`ui::plot_tolerance` — ~0.3 px chord deviation at 300 DPI
  over the *plotted* region, so a circle filling a picked window stays smooth),
* the **PDF device setup and painter** (`ui::write_plot_pdf` → `ui::paint_plot`).

Before this, the tolerance rule and the `QPdfWriter` setup were copy-pasted between
`MainWindow` and `plot_check`, and the paper table lived privately inside the plot
dialog. They are now single definitions with both a GUI and a headless caller.

The CLI injects the same `QtFontEngine` the GUI does, so TrueType/OpenType text plots
as real filled outlines headlessly rather than silently falling back to the stroke font.

### Batch plotting

```sh
for f in sheets/*.musa; do
    musacad --plot "$f" "out/$(basename "${f%.musa}").pdf" --paper A3 --landscape || exit
done
```

`scripts/plot_fixtures.sh` does exactly this over `tests/fixtures/`, writing to
`artifacts/`; a fixture that needs non-default flags carries a sidecar
`<name>.plotargs` file.

## Qt options

Musa CAD's own options are **double-dash** (`--check`, `--help`, `--version`); Qt's
are **single-dash** and are forwarded verbatim, including their values:

```sh
musacad -platform offscreen part.musa
```

An unknown double-dash option is a usage error. Qt's double-dash spellings are not
supported — use the single-dash form.

## Windows

The Windows build (and the installer) ships two programs side by side:

| Program | What it is |
|---|---|
| `musacad_app.exe` | The windowed application — what the Start menu shortcut and the file associations open. No console window. |
| `musacad.exe` | The console front-end for everything on this page. It runs `musacad_app.exe` with the same arguments, hands it the console, waits, and exits with its exit code. |

Use `musacad.exe` from `cmd`, PowerShell and scripts:

```bat
"C:\Program Files\Musa CAD\musacad.exe" --check part.musa && echo ok
```

Two programs because Windows shells do not wait for a windowed program: run directly,
`musacad_app.exe --check part.musa` returns to the prompt at once. It still prints
(the application attaches to the console it was started from), but `%ERRORLEVEL%` /
`$LASTEXITCODE` never sees the result and a `&&` chain runs regardless. The front-end
restores the documented behaviour; redirection (`> out.txt`) and pipes pass through it
unchanged.

`--plot` on Windows asks Qt for `offscreen;windows`: the offscreen platform the
installer ships, with the desktop platform as the fallback, so a deployment that lost
`platforms\qoffscreen.dll` still plots instead of stopping on Qt's error box.

## How it is wired

`main()` parses the command line **before** constructing `QApplication`
(`src/app/main.cpp` → `musacad::app::parse_cli`), which is what lets `--help`,
`--version` and `--check` run headless. The parser lives in its own **Qt-free**
static library (`musacad_cli`, `src/app/cli.cpp`) so it is unit-tested in the normal
test binary; `tests/cli_check.cmake` additionally runs the **shipped executable** with
`DISPLAY`/`WAYLAND_DISPLAY` cleared and asserts each exit code end to end.
