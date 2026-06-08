# ---------------------------------------------------------------------------
# Insertion benchmark (built in every configuration).
# ---------------------------------------------------------------------------
add_executable(musacad_bench_insert bench_insert.cpp)
target_link_libraries(musacad_bench_insert PRIVATE musacad::core)
musacad_enable_sanitizers(musacad_bench_insert)
set_target_properties(musacad_bench_insert PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Command-line demo: drives scripted commands through the processor + engine and
# prints transcripts. Built in every config.
add_executable(musacad_command_demo command_demo.cpp)
target_link_libraries(musacad_command_demo PRIVATE musacad::core musacad::command)
musacad_enable_sanitizers(musacad_command_demo)
set_target_properties(musacad_command_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Offscreen GL render verification + benchmark (real GPU). Built in every config.
add_executable(musacad_render_offscreen render_offscreen.cpp)
target_link_libraries(musacad_render_offscreen PRIVATE
    musacad::core
    musacad::render
    Qt6::Gui
    Qt6::OpenGL)
musacad_enable_sanitizers(musacad_render_offscreen)
set_target_properties(musacad_render_offscreen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Unit tests (Catch2 via FetchContent). Only when MUSACAD_BUILD_TESTS is ON.
# ---------------------------------------------------------------------------
if(NOT MUSACAD_BUILD_TESTS)
    return()
endif()

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(musacad_tests
    test_math.cpp
    test_handles.cpp
    test_geometry_store.cpp
    test_kernel.cpp
    test_triple_buffer.cpp
    test_geometry_engine.cpp
    test_scene_snapshot.cpp
    test_camera.cpp
    test_grid.cpp
    test_frame_stats.cpp
    test_coordinate.cpp
    test_command_processor.cpp
    test_command_roundtrip.cpp
    test_command_suggest.cpp
    test_spatial_grid.cpp
    test_osnap.cpp
    test_erase_pick.cpp
    test_redo.cpp
    test_ortho.cpp
    test_preview_no_mutation.cpp
    test_selection.cpp
    test_modify.cpp
    test_kernel_offset.cpp
    test_osnap_types.cpp
    test_delete_selection.cpp
    test_hover.cpp
    test_transform.cpp
    test_kernel_intersect.cpp
    test_modify_corner.cpp
    test_modify_commands.cpp
    test_modify_polyline.cpp
    test_native_roundtrip.cpp
    test_persistence_engine.cpp
    test_dxf.cpp
    test_layers.cpp
    test_layer_render.cpp
    test_layer_persistence.cpp
    test_text_font.cpp
    test_text_entity.cpp
    test_dimension.cpp
    test_annotation_persistence.cpp
    test_lineweight.cpp
    test_adaptive_tess.cpp
    test_grips.cpp
    test_bulge.cpp
    test_mtext.cpp
    test_header_hygiene.cpp)

target_link_libraries(musacad_tests PRIVATE
    musacad::core
    musacad::render
    musacad::command
    Catch2::Catch2WithMain)

# Sanitizers (ASan/UBSan or TSan) follow the active preset; warnings are not
# promoted to errors for the test target (Catch2 macros are noisy under the
# strict set), but musacad_core is still built warnings-as-errors.
musacad_enable_sanitizers(musacad_tests)

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(musacad_tests)

# ---------------------------------------------------------------------------
# Header-hygiene scan: no external / non-native geometry types may leak through
# the public core headers. Cross-platform via `cmake -P`.
# ---------------------------------------------------------------------------
add_test(
    NAME header_hygiene_scan
    COMMAND ${CMAKE_COMMAND}
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include/musacad/core
            -P ${CMAKE_CURRENT_SOURCE_DIR}/header_scan.cmake)
