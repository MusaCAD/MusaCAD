<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Musa CAD — command line

The `musacad` program opens, checks and plots drawings from a terminal or a script, with
no window: every option, the exit codes, the JSON report of `--check` and ready-to-use
recipes (a folder of drawings to PDF, a CI step, a Windows batch file) are on the wiki:

* **The command line:** https://wiki.musacad.org/scripting/command-line/

Its option tables are read from `musacad --help` when the wiki is built, so they follow
each release. Offline, the short version:

```
musacad [<drawing>]              open a drawing in the GUI
musacad --check <drawing> [--window x0,y0,x1,y1] [--lines] [--json]
                                 read a drawing, check its text, report, exit
musacad --plot <drawing> <out.pdf> [--paper A3] [--portrait] [--scale 1:5] ...
                                 plot to PDF, no display needed
musacad --dump-wiki-data <out.json>
                                 the commands, ribbon and shortcuts as JSON, for the wiki
musacad --help                   every option
```

`<drawing>` is a `.musa` or a `.dxf` file. The exit codes are `0` success, `1` a usage
error, `2` the drawing could not be read, `3` the output could not be written and `4`
`--check` found text problems. Options with one dash (`-platform offscreen`) go to Qt.

On Windows, run `musacad.exe` from scripts, not `musacad_app.exe`, so the script waits for
it and sees its exit code.

How the command line is built -- parsed before any Qt object exists, the plot path it
shares with PLOT, and how `--dump-wiki-data` reads the program -- is in
[ARCHITECTURE.md](ARCHITECTURE.md#the-cli-issue-11).
