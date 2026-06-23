# CompilerWarnings.cmake
#
# musacad_set_warnings(<target> [AS_ERRORS] [BASELINE])
#
# Applies the project's warning flags to <target> as PRIVATE options (they do
# not leak to consumers). Pass AS_ERRORS to promote warnings to errors (used on
# musacad_core). Pass BASELINE for just -Wall -Wextra -Wpedantic (/W4
# /permissive-), used on targets that interface heavily with third-party APIs
# (e.g. the GL backend) where the full strict set is noise.

function(musacad_set_warnings target)
    set(options AS_ERRORS BASELINE)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    set(_gcc_clang_baseline
        -Wall
        -Wextra
        -Wpedantic)

    set(_gcc_clang_strict
        ${_gcc_clang_baseline}
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2)

    set(_msvc_baseline /W4 /permissive-)
    set(_msvc_strict
        ${_msvc_baseline}
        /w14640   # thread-unsafe static member initialization
        /w14826)  # conversion is sign-extended

    if(MSVC)
        if(ARG_BASELINE)
            set(_flags ${_msvc_baseline})
        else()
            set(_flags ${_msvc_strict})
        endif()
        # MSVC /W4 is far stricter than -Wall -Wextra (e.g. C4244 narrowing in <utility>), and the
        # codebase was developed against GCC/Clang. Keep the high warning level for visibility, but
        # only promote to errors (/WX) when MUSACAD_MSVC_WERROR is ON -- off by default until the
        # MSVC warning audit is done, so the Windows release build is not blocked by latent warnings.
        if(ARG_AS_ERRORS AND MUSACAD_MSVC_WERROR)
            list(APPEND _flags /WX)
        endif()
    else()
        if(ARG_BASELINE)
            set(_flags ${_gcc_clang_baseline})
        else()
            set(_flags ${_gcc_clang_strict})
        endif()
        if(ARG_AS_ERRORS)
            list(APPEND _flags -Werror)
        endif()
    endif()

    target_compile_options(${target} PRIVATE ${_flags})
endfunction()
