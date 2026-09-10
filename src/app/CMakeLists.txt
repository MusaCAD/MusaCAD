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
        musacad.manifest
        # Console attachment for the CLI modes of a windowed program (see below).
        win_console.cpp)
    # A windowed program (GUI subsystem): launching it from the Start menu or Explorer must
    # not open a console window behind the CAD window. main() stays main(): Qt's entry-point
    # library supplies the WinMain shim.
    set_target_properties(musacad_app PROPERTIES WIN32_EXECUTABLE ON)

    # musacad.exe -- the console front-end. cmd.exe and PowerShell do not wait for a windowed
    # program, so `musacad_app --check x.musa` returns before the check has run and a script
    # never sees the exit code. The front-end runs musacad_app.exe with the same command
    # line, hands it the console, waits, and exits with its exit code. Win32 only, Qt-free.
    add_executable(musacad_launcher win_launcher.cpp)
    set_target_properties(musacad_launcher PROPERTIES
        OUTPUT_NAME musacad
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    musacad_set_warnings(musacad_launcher)
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
