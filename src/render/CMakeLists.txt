# Embed the GLSL shaders into a generated header at build time (no manual step).
set(_shader_header "${CMAKE_CURRENT_BINARY_DIR}/generated/musacad/render/embedded_shaders.hpp")
musacad_embed_shaders("${_shader_header}"
    "${CMAKE_SOURCE_DIR}/shaders/line.vert"
    "${CMAKE_SOURCE_DIR}/shaders/line.frag"
    "${CMAKE_SOURCE_DIR}/shaders/point.vert"
    "${CMAKE_SOURCE_DIR}/shaders/point.frag"
    "${CMAKE_SOURCE_DIR}/shaders/thickline.vert"
    "${CMAKE_SOURCE_DIR}/shaders/thickline.frag"
    "${CMAKE_SOURCE_DIR}/shaders/fill.vert"
    "${CMAKE_SOURCE_DIR}/shaders/fill.frag")

add_library(musacad_render STATIC
    render_module.cpp
    camera.cpp
    grid.cpp
    frame_stats.cpp
    stroke_font.cpp
    viewport_renderer.cpp
    gl_backend.cpp
    gl/gl_objects.cpp
    "${_shader_header}")

add_library(musacad::render ALIAS musacad_render)

target_include_directories(musacad_render
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"               # for "gl/..." private headers
        "${CMAKE_CURRENT_BINARY_DIR}/generated")    # for the embedded shader header

target_link_libraries(musacad_render
    PUBLIC
        musacad::core
    PRIVATE
        Qt6::Gui
        Qt6::OpenGL)

# The render lib interfaces heavily with the GL backend, so it uses the baseline
# warning set (-Wall -Wextra -Wpedantic) rather than the full strict set.
musacad_set_warnings(musacad_render BASELINE)
musacad_enable_sanitizers(musacad_render)
