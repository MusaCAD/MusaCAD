add_executable(musacad_app
    main.cpp)

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
