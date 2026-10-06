add_library(musacad_core STATIC
    version.cpp
    crash_report.cpp
    geometry_store.cpp
    native_kernel_2d.cpp
    spatial_grid.cpp
    entity_bounds.cpp
    osnap.cpp
    scene_snapshot.cpp
    tiled_viewport.cpp
    geometry_engine.cpp
    io/document.cpp
    io/native_format.cpp
    io/dxf.cpp
    io/external_files.cpp
    text/stroke_font.cpp
    text/mtext.cpp
    text/text_codes.cpp
    text/text_frame.cpp
    text_check.cpp
    hatch.cpp
    hatch_pattern.cpp
    dimension.cpp
    gdt.cpp
    image.cpp
    table.cpp
    block_resolve.cpp
    grips.cpp
    properties_registry.cpp
    linetype.cpp)

add_library(musacad::core ALIAS musacad_core)

target_include_directories(musacad_core PUBLIC
    "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(musacad_core PRIVATE
    MUSACAD_VERSION="${PROJECT_VERSION}")

# std::jthread / std::condition_variable_any need the platform thread library.
find_package(Threads REQUIRED)
target_link_libraries(musacad_core PUBLIC Threads::Threads)

# The engine with GCC: its variable tracking (where each variable lives in optimised code,
# for the debugger) is most of what geometry_engine.cpp takes to compile with -O2 -g --
# 16.5 GB with it, 8.6 GB without, more than a packaging builder has. Line tables, and the
# call stacks of a crash report, are not affected; it is off for this file only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(geometry_engine.cpp PROPERTIES COMPILE_OPTIONS "-fno-var-tracking")
endif()

# musacad_core builds with warnings promoted to errors.
musacad_set_warnings(musacad_core AS_ERRORS)
musacad_enable_sanitizers(musacad_core)
