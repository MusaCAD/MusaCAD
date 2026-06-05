add_library(musacad_ui STATIC
    main_window.cpp
    viewport_window.cpp
    command_line_widget.cpp
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/main_window.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/viewport_window.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/command_line_widget.hpp"
    "${CMAKE_SOURCE_DIR}/include/musacad/ui/viewport_modes.hpp")

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
    PRIVATE
        Qt6::OpenGL)

musacad_set_warnings(musacad_ui BASELINE)
musacad_enable_sanitizers(musacad_ui)
