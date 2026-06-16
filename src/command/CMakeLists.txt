add_library(musacad_command STATIC
    command_module.cpp
    coordinate.cpp
    commands.cpp
    dyn_fields.cpp
    command_registry.cpp
    command_processor.cpp)

add_library(musacad::command ALIAS musacad_command)

target_include_directories(musacad_command PUBLIC
    "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(musacad_command
    PUBLIC
        musacad::core)

# Baseline warnings (-Wall -Wextra -Wpedantic) per the phase spec.
musacad_set_warnings(musacad_command BASELINE)
musacad_enable_sanitizers(musacad_command)
