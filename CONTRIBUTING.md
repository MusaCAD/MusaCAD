# Contributing to Musa CAD

Bug reports, testing, documentation, accessibility feedback, and code contributions
are welcome. Please follow the [Code of Conduct](https://github.com/MusaCAD/.github/blob/main/CODE_OF_CONDUCT.md).
The [wiki contribution guide](https://wiki.musacad.org/developer/contributing/)
has more detail, including how to help with documentation.

## Report bugs and suggest features

Search [existing issues](https://github.com/MusaCAD/MusaCAD/issues) before opening a
new one. Include the Musa CAD version and build from **About** or `musacad --version`,
your operating system, installation method, and steps to reproduce. Describe the
actual and expected results. A small drawing that reproduces the problem is especially
useful; share only drawings you have permission to publish and remove private details
from logs and crash reports.

For an AutoCAD compatibility request, describe the command, prompts, and behavior in
your own words, and link to the relevant Autodesk documentation when available.
Include the AutoCAD version if known. Do not copy proprietary code, documentation,
icons, or other assets into a contribution.

Use the [security policy](SECURITY.md) for vulnerabilities and the
[accessibility reporting guidance](ACCESSIBILITY.md#report-a-barrier) for access needs.
Questions about using Musa CAD can go in the issue tracker; the
[wiki](https://wiki.musacad.org/) is the first place to look for command help.

## Make a change

Discuss substantial features, dependencies, or architectural changes in an issue
first. Small fixes can go straight to a pull request.

1. Fork the repository and create a branch named for the change.
2. Follow the surrounding C++23 and Qt style. Read the
   [architecture](docs/ARCHITECTURE.md) and [codebase tour](https://wiki.musacad.org/developer/codebase-tour/)
   before changing thread boundaries, geometry, or rendering.
3. Keep commits focused and write subjects that describe the product change.
4. Update `docs/COMMANDS.md` for command changes, relevant wiki documentation, and
   `CHANGELOG.md` for user-visible behavior changes.
5. Open a pull request describing the problem, resulting behavior, and validation.
   Link the issue; use `Fixes #...` only if all of its requirements are satisfied.

Preserve existing license and copyright notices. New source files need an appropriate
SPDX header, following the surrounding files. Contributions must be compatible with
LGPL-3.0-or-later. Discuss dependencies before adding them: Musa CAD keeps its own
geometry kernel, and DWG conversion runs through separate converter programs.
See [third-party components](docs/THIRD_PARTY_LICENSES.md).

Contributors can add an entry to [CONTRIBUTORS.md](CONTRIBUTORS.md).

## Build and validate

Install the prerequisites in the [build guide](https://wiki.musacad.org/developer/building/),
then run:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset enables the test suite and AddressSanitizer/UBSan where supported.
For an optimized application build:

```sh
cmake --preset release
cmake --build --preset release
```

Add a regression test for a bug or changed behavior where practical. Run focused
checks during development and the relevant suite before submitting. For UI changes,
test the actual command flow, keyboard focus, cancellation, and display scaling;
include screenshots when they help explain the result. For file-format changes,
check save/reopen and compatibility with existing drawings.

Documentation-only changes need link and formatting checks, not a full C++ build.
State which platforms and tests were checked and any limitations. The
[test guide](https://wiki.musacad.org/developer/tests/) covers the available suites.
