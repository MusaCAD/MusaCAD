# musacad_embed_shaders(<output_hpp> <shader_file>...)
#
# Adds a build step that regenerates <output_hpp> (embedding the given shader
# files as string literals) whenever any shader changes. A clean build produces
# the header with no manual step. Cross-platform: the generator runs via
# `cmake -P`.

function(musacad_embed_shaders output)
    set(shaders ${ARGN})
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND}
                -DOUT=${output}
                "-DFILES=${shaders}"
                -P "${CMAKE_SOURCE_DIR}/cmake/embed_shaders_script.cmake"
        DEPENDS ${shaders} "${CMAKE_SOURCE_DIR}/cmake/embed_shaders_script.cmake"
        COMMENT "Embedding shaders -> ${output}"
        VERBATIM)
endfunction()
