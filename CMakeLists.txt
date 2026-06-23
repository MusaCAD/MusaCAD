cmake_minimum_required(VERSION 3.25)

project(MusaCAD
    VERSION 0.1.0
    DESCRIPTION "Modern cross-platform multi-threaded 2D/3D CAD engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global language settings: C++23, strict, no compiler extensions.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Only the top-level project drives these global conveniences.
if(PROJECT_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
endif()

# Generate compile_commands.json for tooling (clangd, clang-tidy, IDEs).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Project options.
# ---------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Enable ASan+UBSan (Debug builds)" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ENABLE_SANITIZERS)" OFF)
option(MUSACAD_BUILD_TESTS "Build unit tests" OFF)

if(ENABLE_SANITIZERS AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_SANITIZERS (ASan+UBSan) and ENABLE_TSAN cannot be combined")
endif()

# ---------------------------------------------------------------------------
# Shared CMake modules.
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(EmbedShaders)

# ---------------------------------------------------------------------------
# External dependencies.
#
# Strategy: Qt6 and Vulkan are documented system prerequisites (see
# docs/BUILD.md) -- they are the only unavoidable manual installs. Geometry
# math uses our own types (Phase 2); the Catch2 test framework is fetched via
# FetchContent and wired starting in Phase 2 when the first tests exist.
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets PrintSupport)
# Vulkan headers/loader are located so the GPU-backend seam can be validated WHEN PRESENT, but
# the active backend is OpenGL 4.6 Core and Vulkan is NOT linked into the binary (see
# docs/ARCHITECTURE.md). It is therefore optional -- nothing depends on the Vulkan:: targets, so
# CI runners / platforms without a Vulkan SDK (e.g. the Windows installer build) build identically.
find_package(Vulkan)

# Qt: automatic moc/uic/rcc handling for QObject-derived types.
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Targets.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/render)
add_subdirectory(src/command)
add_subdirectory(src/ui)
add_subdirectory(src/app)

# tests/ holds the Catch2 unit tests (built when MUSACAD_BUILD_TESTS is ON) and
# the insertion benchmark (built in every config, so it can be timed under the
# release preset). The subdirectory gates the unit tests internally.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Configuration summary.
# ---------------------------------------------------------------------------
message(STATUS "------------------------------------------------------------")
message(STATUS "Musa CAD ${PROJECT_VERSION}")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard      : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Sanitizers (A+UB) : ${ENABLE_SANITIZERS}")
message(STATUS "  ThreadSanitizer   : ${ENABLE_TSAN}")
message(STATUS "  Build tests       : ${MUSACAD_BUILD_TESTS}")
message(STATUS "  Qt6 version       : ${Qt6_VERSION}")
if(Vulkan_FOUND)
    message(STATUS "  Vulkan (seam)     : ${Vulkan_VERSION}")
else()
    message(STATUS "  Vulkan (seam)     : not found (optional; OpenGL backend in use)")
endif()
message(STATUS "------------------------------------------------------------")
