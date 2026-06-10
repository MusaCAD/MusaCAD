# Building Musa CAD

Musa CAD is designed to be **clone-and-build**. Aside from a small set of
unavoidable system prerequisites (a C++23 compiler, Qt6, and the Vulkan
loader/headers), everything else is provided by the build system.

```sh
cmake --preset dev          # configure (Debug + ASan/UBSan + tests)
cmake --build --preset dev  # build
```

The runnable lands at `build/dev/bin/musacad_app`.

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```

## System prerequisites

| Prerequisite        | Minimum | Notes |
|---------------------|---------|-------|
| C++23 compiler      | GCC 13+, Clang 17+, or MSVC 19.38+ (VS 2022 17.8) | `-std=c++23`, no extensions |
| CMake               | 3.25+   | Presets v6 |
| Ninja               | 1.10+   | Default generator in the presets |
| Qt6 (Core/Gui/Widgets) | 6.4+ | UI frame and event loop |
| Vulkan SDK / loader | 1.3+    | GPU backend (used from Phase 3); headers + loader required to configure |

### Linux (Debian/Ubuntu)

```sh
sudo apt-get install -y build-essential cmake ninja-build \
    qt6-base-dev libvulkan-dev
```

For GPU rendering at runtime you also need working Vulkan drivers
(`mesa-vulkan-drivers` for Intel/AMD, or the NVIDIA proprietary driver).

### Windows

* Visual Studio 2022 (17.8+) with the "Desktop development with C++" workload.
* CMake and Ninja (bundled with VS, or install standalone).
* Qt6 — install via the [Qt online installer](https://www.qt.io/download) and
  point CMake at it with `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64`, or use a
  vcpkg manifest.
* Vulkan SDK from [LunarG](https://vulkan.lunarg.com/).

## Dependency strategy

* **Qt6** and **Vulkan** are located with `find_package` and are the only
  unavoidable manual installs (table above).
* **Geometry math** uses our own `Vec`/`Mat` types (no `glm`); introduced in
  Phase 2.
* **Catch2** (unit tests) is fetched via CMake `FetchContent` and wired in
  Phase 2. Tests build only when `MUSACAD_BUILD_TESTS=ON` (the `dev` preset
  sets it).

No geometry/CAD kernel is a dependency: Musa CAD ships its own native 2D
kernel behind the `IGeometryKernel` interface.

## DWG import/export (optional external converter)

DWG support is **not built in** and adds **no build dependency**. Musa CAD is
LGPL; the DWG converters are GPL (LibreDWG) or proprietary (ODA File Converter),
so Musa CAD never links, bundles, or vendors them. Instead it *invokes a converter
you install* as a subprocess and reads the DXF it produces — the licensing boundary
is a process boundary, keeping Musa CAD's code and shipped binaries GPL-free.

To enable DWG import/export, install **one** converter:

* **ODA File Converter** (free, from opendesign.com) — put `ODAFileConverter` on
  your `PATH`, or
* **LibreDWG** (`dwg2dxf` / `dxf2dwg`) — e.g. `sudo apt-get install libredwg-bin`,
  or build from source; put `dwg2dxf` on your `PATH`.

Musa CAD auto-detects either on `PATH`. Use the **"DWG Setup"** button (File panel)
to see what was detected, **Browse** to a specific binary (or a custom wrapper
invoked as `converter <in> <out>`), auto-detect on `PATH`, or open the download
pages — it saves the `io/dwg_converter_path` setting for you. If no converter is
found, Import/Export DWG shows that dialog via a "Configure…" button — nothing
crashes. (Musa CAD does not download/install the converter for you: licensing, the
ODA EULA, and per-platform installers make that the user's step.)

## Sanitizers

The `dev` preset sets `ENABLE_SANITIZERS=ON`, enabling AddressSanitizer and
UndefinedBehaviorSanitizer (GCC/Clang) or AddressSanitizer (MSVC) across all
targets.

### ThreadSanitizer (`tsan` preset)

For the concurrency tests (triple-buffer handoff, MPSC queue), a separate
`tsan` preset builds with ThreadSanitizer (mutually exclusive with ASan/UBSan):

```sh
cmake --preset tsan && cmake --build --preset tsan
ctest --preset tsan
```

On recent Linux kernels TSan can abort at startup with
`FATAL: ThreadSanitizer: unexpected memory mapping` because the default ASLR
entropy is higher than TSan's shadow-memory layout expects. If you hit this,
lower it for the session:

```sh
sudo sysctl -w vm.mmap_rnd_bits=28
```

(Clang's TSan needs a `libstdc++`/`libc++` it can link; GCC works out of the
box and is what these presets assume.)

### CI note (important)

A CI runner that builds the `tsan` preset **must** lower the ASLR entropy before
running the TSan binaries, or they abort with
`FATAL: ThreadSanitizer: unexpected memory mapping`:

```sh
sudo sysctl -w vm.mmap_rnd_bits=28   # once per runner/boot, before ctest --preset tsan
```

For the **ASan smoke run of the GUI app** (`MUSACAD_SMOKE=1 musacad_app`), the
windowing/driver stack (libwayland, the Qt platform plugin, Mesa) reports its
own leaks that are not Musa CAD's. Point LeakSanitizer at the suppression file
so only our leaks surface:

```sh
MUSACAD_SMOKE=1 \
  LSAN_OPTIONS=suppressions=tools/lsan.supp \
  ./build/dev/bin/musacad_app
```

## Headless / CI smoke test

Setting `MUSACAD_SMOKE` makes the app show its window and quit promptly, so a
clean, leak-free startup can be verified without a display:

```sh
MUSACAD_SMOKE=1 QT_QPA_PLATFORM=offscreen ./build/dev/bin/musacad_app
```
