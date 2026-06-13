add_executable(musacad_app
    main.cpp
    # Branding logo embedded as a Qt resource (:/branding/musacad_logo.svg) for the
    # window icon + About dialog. AUTORCC compiles the .qrc.
    ${CMAKE_SOURCE_DIR}/assets/branding/branding.qrc)
set_target_properties(musacad_app PROPERTIES AUTORCC ON)

# Windows: carry the multi-resolution icon in the .exe (regen_icons.sh builds the .ico).
if(WIN32)
    target_sources(musacad_app PRIVATE ${CMAKE_SOURCE_DIR}/assets/branding/musacad.rc)
endif()

target_link_libraries(musacad_app
    PRIVATE
        musacad::core
        musacad::render
        musacad::command
        musacad::ui
        Qt6::Widgets)

musacad_set_warnings(musacad_app)
musacad_enable_sanitizers(musacad_app)

# Place the runnable next to a predictable path.
set_target_properties(musacad_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
