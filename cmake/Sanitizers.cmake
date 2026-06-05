# Sanitizers.cmake
#
# musacad_enable_sanitizers(<target>)
#
# When ENABLE_SANITIZERS is ON, wires AddressSanitizer + UndefinedBehavior
# Sanitizer into <target> (compile and link). No-op otherwise. The `dev`
# preset turns ENABLE_SANITIZERS ON.
#
# Notes:
#   * GCC/Clang get ASan + UBSan with frame pointers preserved for readable
#     stack traces.
#   * MSVC supports /fsanitize=address only (no UBSan); we apply that.

function(musacad_enable_sanitizers target)
    if(ENABLE_TSAN)
        # ThreadSanitizer (GCC/Clang). Cannot be combined with ASan/UBSan.
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
        message(STATUS "ThreadSanitizer enabled for ${target}")
        return()
    endif()

    if(NOT ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        # MSVC links the ASan runtime automatically; no extra link flags.
        message(STATUS "Sanitizers (ASan) enabled for ${target}")
        return()
    endif()

    set(_san_flags
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)

    target_compile_options(${target} PRIVATE ${_san_flags})
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    message(STATUS "Sanitizers (ASan+UBSan) enabled for ${target}")
endfunction()
