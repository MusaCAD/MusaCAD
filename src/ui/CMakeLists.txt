add_library(musacad_ui STATIC
    main_window.cpp
    viewport_window.cpp
    command_line_widget.cpp
    parameter_dialog.cpp
    layer_dialog.cpp
    properties_panel.cpp
    dyn_input.cpp
    dwg_converter.cpp
    dwg_installer.cpp
    update_checker.cpp
    update_installer.cpp
    qt_font_engine.cpp
    qt_image_decoder.cpp
    plot.cpp
    plot_dialog.cpp
    ribbon_bar.cpp
    ribbon_layout.cpp
    command_icons.cpp
    theme.cpp
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/properties_panel.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/dyn_input.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/main_window.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/viewport_window.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/command_line_widget.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/parameter_dialog.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/layer_dialog.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/plot_dialog.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/ribbon_bar.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/viewport_modes.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/dwg_installer.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/update_checker.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/update_installer.hpp")

add_library(musacad::ui ALIAS musacad_ui)

# QObject-derived headers in this target need moc.
set_target_properties(musacad_ui PROPERTIES AUTOMOC ON)

target_include_directories(musacad_ui PUBLIC
    "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(musacad_ui
    PUBLIC
        musacad::core
        musacad::render
        musacad::command
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::PrintSupport
    PRIVATE
        Qt6::OpenGL
        Qt6::Network)

# Windows: the in-place updater reads the downloaded installer's version resource
# (version.lib), checks its Authenticode signature (wintrust.lib) and starts it elevated
# (shell32.lib).
if(WIN32)
    target_link_libraries(musacad_ui PRIVATE version wintrust shell32)
endif()

musacad_set_warnings(musacad_ui BASELINE)
musacad_enable_sanitizers(musacad_ui)
