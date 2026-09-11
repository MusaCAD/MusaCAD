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

# Headless plot verification harness (manual): plots a known circle to a PDF.
add_executable(musacad_plot_check ${CMAKE_CURRENT_SOURCE_DIR}/../tools/plot_check.cpp)
target_link_libraries(musacad_plot_check PRIVATE
    musacad::core
    musacad::render
    musacad::ui
    Qt6::Gui
    Qt6::Widgets)
musacad_enable_sanitizers(musacad_plot_check)
set_target_properties(musacad_plot_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Branding raster generator: SVG -> PNGs via Qt (assets/branding/regen_icons.sh drives it).
add_executable(musacad_svg2png ${CMAKE_CURRENT_SOURCE_DIR}/../tools/svg2png.cpp)
target_link_libraries(musacad_svg2png PRIVATE Qt6::Gui)
musacad_enable_sanitizers(musacad_svg2png)
set_target_properties(musacad_svg2png PROPERTIES
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
if(MSVC)
    # MSVC's STL records per object whether its container annotations were compiled in
    # (/fsanitize=address) and refuses to link objects that disagree (LNK2038
    # annotate_string / annotate_vector). Catch2 is built from source here, so under the
    # sanitizer presets it simply gets the same flags as the test binary that links it.
    musacad_enable_sanitizers(Catch2)
    musacad_enable_sanitizers(Catch2WithMain)
endif()

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
    test_dyn_fields.cpp
    test_command_suggest.cpp
    test_spatial_grid.cpp
    test_osnap.cpp
    test_erase_pick.cpp
    test_redo.cpp
    test_ortho.cpp
    test_preview_no_mutation.cpp
    test_selection.cpp
    test_modify.cpp
    test_stretch.cpp
    test_array.cpp
    test_divide.cpp
    test_polygon.cpp
    test_polyline_ops.cpp
    test_rectangle.cpp
    test_revcloud.cpp
    test_explode.cpp
    test_xline.cpp
    test_ellipse.cpp
    test_spline.cpp
    test_modify_curves.cpp
    test_dim_types.cpp
    test_views_groups.cpp
    test_units.cpp
    test_text_style.cpp
    test_undo_remap.cpp
    test_break.cpp
    test_align_lengthen.cpp
    test_inquiry.cpp
    test_multidoc.cpp
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
    test_dim_fit.cpp
    test_dim_chain.cpp
    test_dim_text_edit.cpp
    test_annotation_persistence.cpp
    test_lineweight.cpp
    test_plot_text_weight.cpp
    test_adaptive_tess.cpp
    test_grips.cpp
    test_bulge.cpp
    test_mtext.cpp
    test_text_codes.cpp
    test_hatch.cpp
    test_gdt.cpp
    test_table.cpp
    test_image.cpp
    test_blocks.cpp
    test_block_authoring.cpp
    test_dxf_tolerance.cpp
    test_pedit.cpp
    test_snaps_input.cpp
    test_wipeout_field.cpp
    test_dxf_legacy_polyline.cpp
    test_attributes.cpp
    test_refedit.cpp
    test_polyline_grips.cpp
    test_image_commands.cpp
    test_layouts.cpp
    test_viewports.cpp
    test_mspace.cpp
    test_xref.cpp
    test_image_polyclip.cpp
    test_fonts.cpp
    test_properties.cpp
    test_linetype.cpp
    test_cli.cpp
    test_header_hygiene.cpp)

target_link_libraries(musacad_tests PRIVATE
    musacad::core
    musacad::cli
    musacad::render
    musacad::command
    Catch2::Catch2WithMain)

# Sanitizers (ASan/UBSan or TSan) follow the active preset; warnings are not
# promoted to errors for the test target (Catch2 macros are noisy under the
# strict set), but musacad_core is still built warnings-as-errors.
musacad_enable_sanitizers(musacad_tests)

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
if(WIN32)
    # The test binary links Qt (through musacad_render) and, under the dev preset, the
    # AddressSanitizer runtime and the debug CRT. None of those is on PATH by itself, and
    # Catch2 3.5's DL_PATHS REPLACES PATH while it lists the test cases right after the link
    # (it prepends at test time), so the list has to be complete: the Qt this build was
    # configured with, the compiler's own directory (clang_rt.asan_dynamic, msvcp140d and
    # friends live next to cl.exe), and the SDK's ucrt folder (ucrtbased.dll; a full Visual
    # Studio install also copies it to System32, a bare toolset does not).
    set(_musacad_dl_paths "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    get_filename_component(_musacad_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND _musacad_dl_paths "${_musacad_cxx_dir}")
    if(CMAKE_MT)
        get_filename_component(_musacad_sdk_bin "${CMAKE_MT}" DIRECTORY)
        if(EXISTS "${_musacad_sdk_bin}/ucrt")
            list(APPEND _musacad_dl_paths "${_musacad_sdk_bin}/ucrt")
        endif()
    endif()
    catch_discover_tests(musacad_tests DL_PATHS ${_musacad_dl_paths})
    # The same directories, as CTest environment edits, for the tests below that run the
    # application binary.
    set(_musacad_path_mods "")
    foreach(_dir IN LISTS _musacad_dl_paths)
        list(APPEND _musacad_path_mods "PATH=path_list_prepend:${_dir}")
    endforeach()
else()
    catch_discover_tests(musacad_tests)
endif()

# ---------------------------------------------------------------------------
# Header-hygiene scan: no external / non-native geometry types may leak through
# the public core headers. Cross-platform via `cmake -P`.
# ---------------------------------------------------------------------------
add_test(
    NAME header_hygiene_scan
    COMMAND ${CMAKE_COMMAND}
            -DINCLUDE_DIR=${CMAKE_SOURCE_DIR}/include/musacad/core
            -P ${CMAKE_CURRENT_SOURCE_DIR}/header_scan.cmake)

# ---------------------------------------------------------------------------
# CLI end-to-end: run the SHIPPED binary and assert its exit codes with no
# display available (issue #11). Cross-platform via `cmake -P`, like the scan above.
# ---------------------------------------------------------------------------
add_test(
    NAME cli_check
    COMMAND ${CMAKE_COMMAND}
            -DAPP=$<TARGET_FILE:musacad_app>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_work
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_check.cmake)

# This is the only test that EXECUTES the Qt application binary, so it is the only one
# that meets Qt/glib's own sanitizer noise: QGuiApplication starts a QDBusConnection
# thread that races with glib's event loop inside libglib/libQt6DBus (no Musa CAD frame
# in either stack, and it happens with every platform plugin). Point TSan at the
# third-party suppression file for THIS test only -- the concurrency tests keep running
# with no suppressions at all. Same shape as the LSan suppression the GUI smoke run uses.
if(ENABLE_TSAN)
    set_tests_properties(cli_check PROPERTIES
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/tsan.supp")
endif()
# Windows: the application binary needs the Qt DLLs (and, under the dev preset, the ASan
# runtime) on PATH to start -- the same directories as for the unit-test binary above.
if(WIN32)
    set_tests_properties(cli_check PROPERTIES ENVIRONMENT_MODIFICATION "${_musacad_path_mods}")
endif()
