# Building Musa CAD

How to build Musa CAD on Linux, Windows and macOS -- the presets, DWG support through an
external converter, the sanitizers and the headless smoke test -- is on the wiki:

* **Building:** https://wiki.musacad.org/developer/building/
* **Packaging** (the AppImage, the Flatpak, the Windows installer, the macOS disk image):
  https://wiki.musacad.org/developer/packaging/

Offline, the short version. You need a C++23 compiler (GCC 13+, Clang 17+ or MSVC 19.38+),
CMake 3.25+, Ninja and Qt 6.4+; on Debian or Ubuntu:

```sh
sudo apt-get install -y build-essential cmake ninja-build qt6-base-dev libvulkan-dev
```

Then:

```sh
cmake --preset release && cmake --build --preset release   # build/release/bin/musacad_app
cmake --preset dev && cmake --build --preset dev           # Debug, ASan/UBSan and the tests
```
