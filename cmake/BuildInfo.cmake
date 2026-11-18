# Source provenance, not a wall-clock timestamp: reproducible packagers can replace
# __DATE__/__TIME__ or SOURCE_DATE_EPOCH without changing the identity shown in the app.
set(revision "source archive")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE git_revision OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result ERROR_QUIET)
    if(git_result EQUAL 0 AND git_revision MATCHES "^[0-9a-f]+$")
        set(revision "${git_revision}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" diff-index --quiet HEAD --
            RESULT_VARIABLE dirty ERROR_QUIET)
        if(dirty EQUAL 1)
            string(APPEND revision " (modified)")
        endif()
    endif()
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_DIR}/build_info.hpp.tmp"
    "// Generated source revision. Do not edit.\n#pragma once\n#define MUSACAD_BUILD_ID \"${revision}\"\n")
configure_file("${OUTPUT_DIR}/build_info.hpp.tmp" "${OUTPUT_DIR}/build_info.hpp" COPYONLY)
file(REMOVE "${OUTPUT_DIR}/build_info.hpp.tmp")
