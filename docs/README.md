# Repository documentation

The [wiki](https://wiki.musacad.org/) contains installation help, tutorials, the user
manual, command reference and scripting guides. Start there when using Musa CAD.

These documents stay with the source because they describe implementation details,
release procedures or licensing:

| Document | Purpose |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | Thread boundaries, storage, rendering and implementation notes. |
| [Command status](COMMANDS.md) | Command behavior and remaining compatibility gaps. |
| [Settings status](AUTOCAD_CONFIG.md) | Implemented and planned settings. |
| [Roadmap](ROADMAP.md) | Links to the active work and earlier planning records. |
| [Releasing](RELEASING.md) | Checks, packaging and publication steps. |
| [Windows signing](WINDOWS_SIGNING.md) | Current updater checks and signing integration. |
| [Third-party licenses](THIRD_PARTY_LICENSES.md) | Dependencies and license notices. |
| [Release notes](release-notes/) | Historical notes for each tagged release. |

The wiki imports the architecture, command and settings tables, roadmap, license
notes and release notes from a pinned release. Keep these paths available when
updating the source documents. A change on `main` may describe work that has not
shipped yet; the wiki's version label identifies the release its guides cover.

The short [build](BUILD.md) and [CLI](CLI.md) pages keep existing links useful and
point to the full wiki guides. Packaging notes beside the scripts describe how to
build and verify each artifact. Community policies remain at the repository root
so GitHub and contributors can find them.

Keep tutorials in the wiki. Update implementation references with the code, and use
[issues](https://github.com/MusaCAD/MusaCAD/issues) for current bug reports and feature
work instead of adding another status checklist.
