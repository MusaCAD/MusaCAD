# The shipped binary's command-line parser. Its own Qt-FREE static library so it can be
# unit-tested without linking the GUI (musacad_tests links core/render/command only) and
# so main() can parse argv before any Qt object exists.
add_library(musacad_cli STATIC cli.cpp)
add_library(musacad::cli ALIAS musacad_cli)
target_include_directories(musacad_cli PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(musacad_cli PUBLIC musacad::core)
musacad_set_warnings(musacad_cli AS_ERRORS)
musacad_enable_sanitizers(musacad_cli)

add_executable(musacad_app
    main.cpp
    # The --plot runner: needs Qt (QPdfWriter via ui::write_plot_pdf), so it lives here
    # rather than in the Qt-free musacad_cli parser library.
    plot_cli.cpp
    # Branding logo embedded as a Qt resource (:/branding/musacad_logo.svg) for the
    # window icon + About dialog. AUTORCC compiles the .qrc.
    ${CMAKE_SOURCE_DIR}/assets/branding/branding.qrc
    # Ribbon command icons (:/ribbon/*.svg) -- Musa-authored SVGs, registry-driven.
    ${CMAKE_SOURCE_DIR}/assets/ribbon/ribbon.qrc)
set_target_properties(musacad_app PROPERTIES AUTORCC ON)

# Windows.
if(WIN32)
    target_sources(musacad_app PRIVATE
        # The multi-resolution icon carried in the .exe (regen_icons.sh builds the .ico).
        ${CMAKE_SOURCE_DIR}/assets/branding/musacad.rc
        # UTF-8 process code page (non-ASCII drawing paths) + long-path awareness. MSVC's
        # linker merges a .manifest source into the executable's manifest.
        musacad.manifest)
endif()

target_link_libraries(musacad_app
    PRIVATE
        musacad::core
        musacad::cli
        musacad::render
        musacad::command
        musacad::ui
        Qt6::Widgets)

musacad_set_warnings(musacad_app)
musacad_enable_sanitizers(musacad_app)

# Place the runnable next to a predictable path.
set_target_properties(musacad_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
