# Scans the public core headers for includes of external / non-native geometry
# types. The geometry kernel and data model must speak only our own types, so
# none of Qt, Vulkan, OpenGL, or a third-party math/geometry library may appear
# in a public include/musacad/core header. Run via: cmake -P header_scan.cmake
#
# Requires -DINCLUDE_DIR=<path to include/musacad/core>.

if(NOT DEFINED INCLUDE_DIR)
    message(FATAL_ERROR "INCLUDE_DIR not set")
endif()

file(GLOB_RECURSE headers "${INCLUDE_DIR}/*.hpp")

# Forbidden include tokens (matched inside #include lines).
set(forbidden
    "QtCore" "QtGui" "QtWidgets" "QApplication" "QWidget" "QObject" "<Q"
    "vulkan/" "vulkan.h" "<GL/" "GL/gl" "glad" "glew"
    "glm/" "Eigen/" "opencascade" "BRep" "TopoDS" "gp_Pnt")

set(violations "")
foreach(header IN LISTS headers)
    file(STRINGS "${header}" include_lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS include_lines)
        foreach(token IN LISTS forbidden)
            string(FIND "${line}" "${token}" pos)
            if(NOT pos EQUAL -1)
                list(APPEND violations "${header}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()

list(LENGTH headers header_count)
if(violations)
    message(FATAL_ERROR "Non-native types leaked into public core headers:\n${violations}")
endif()

message(STATUS "header_hygiene: scanned ${header_count} core headers, no external types found")
