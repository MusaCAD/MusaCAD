# End-to-end check of the shipped binary's command line (issue #11).
#
# The Catch2 cases in test_cli.cpp cover the parser; this asserts what a SCRIPT sees:
# the real executable's exit codes and output, with no display available. Run via
#   cmake -DAPP=<path to musacad_app> -DWORK=<scratch dir> -P cli_check.cmake
# Mirrors header_scan.cmake -- cross-platform, no shell required.

if(NOT DEFINED APP)
    message(FATAL_ERROR "APP not set")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "WORK not set")
endif()
file(MAKE_DIRECTORY "${WORK}")

# No display: --help/--version/--check must not need one. QT_QPA_PLATFORM is deliberately
# NOT set -- these paths must return before any Qt application object is constructed.
set(ENV{DISPLAY} "")
set(ENV{WAYLAND_DISPLAY} "")

set(failures "")

# run(<expected exit code> <output variable> <args...>)
function(run expect out_var)
    execute_process(COMMAND "${APP}" ${ARGN}
                    RESULT_VARIABLE code
                    OUTPUT_VARIABLE stdout_text
                    ERROR_VARIABLE stderr_text
                    TIMEOUT 60)
    set(${out_var} "${stdout_text}${stderr_text}" PARENT_SCOPE)
    if(NOT code EQUAL expect)
        set(failures "${failures};`musacad ${ARGN}` exited ${code}, expected ${expect}" PARENT_SCOPE)
    endif()
endfunction()

# --- 0: --help and --version succeed with no display -------------------------------
run(0 help_out --help)
if(NOT help_out MATCHES "Usage:")
    list(APPEND failures "--help did not print a Usage: section")
endif()
if(NOT help_out MATCHES "--check")
    list(APPEND failures "--help did not document --check")
endif()

run(0 ver_out --version)
if(NOT ver_out MATCHES "Musa CAD")
    list(APPEND failures "--version did not name the application")
endif()

# --- 1: usage errors exit 1 --------------------------------------------------------
run(1 e1 --nonsense)
run(1 e2 --check)

# --- 2: --check on a valid drawing exits 0 -----------------------------------------
# A minimal but genuine v14 document. Written at version 14 on purpose: as the format
# advances this doubles as an older-version-still-loads check from the CLI's side.
set(good "${WORK}/cli_good.musa")
file(WRITE "${good}"
"MUSACAD 14
UNITS mm
CURRENT 0
LTSCALE 1
LAYER 255 255 255 0 25 1 0 0 0
LINE 0 0 100 0 0 7 255 255 255 0 25
LINE 100 0 100 50 0 7 255 255 255 0 25
END
")
run(0 good_out --check "${good}")

# --- 3: --check on a malformed drawing exits 2 and says why on stderr ---------------
set(bad "${WORK}/cli_bad.musa")
file(WRITE "${bad}"
"MUSACAD 14
LINE this is not a coordinate
END
")
run(2 bad_out --check "${bad}")
if(NOT bad_out MATCHES "LINE")
    list(APPEND failures "--check on a malformed file did not report the offending record")
endif()

# --- 4: --check on a missing file exits 2 ------------------------------------------
run(2 miss_out --check "${WORK}/definitely_absent.musa")

# --- 5: --plot writes a structurally valid PDF, with no display --------------------
# NOTE the environment: DISPLAY/WAYLAND_DISPLAY are cleared above, and QT_QPA_PLATFORM
# is deliberately left as the desktop session set it (often "wayland;xcb"). --plot must
# still work -- that inherited value is exactly what breaks a cron/CI plot, so forcing
# offscreen is part of the contract, not an optimisation.
set(pdf "${WORK}/cli_plot.pdf")
file(REMOVE "${pdf}")
run(0 plot_out --plot "${good}" "${pdf}" --paper A4 --portrait)
if(EXISTS "${pdf}")
    file(SIZE "${pdf}" pdf_size)
    if(pdf_size LESS 400)
        list(APPEND failures "--plot produced a suspiciously small PDF (${pdf_size} bytes)")
    endif()
    # Structural validity, not a byte comparison: a real PDF starts with %PDF- and ends
    # with %%EOF. Byte-comparing plots would be flaky; the geometry itself is asserted
    # numerically by the unit tests. Read as HEX -- a PDF has binary streams, so CMake's
    # text file(READ) truncates at the first NUL and would report a false failure.
    file(READ "${pdf}" pdf_head_hex LIMIT 5 HEX)
    if(NOT pdf_head_hex STREQUAL "255044462d") # "%PDF-"
        list(APPEND failures "--plot output does not start with the %PDF- signature")
    endif()
    file(READ "${pdf}" pdf_hex HEX)
    string(FIND "${pdf_hex}" "2525454f46" eof_pos REVERSE) # "%%EOF"
    if(eof_pos EQUAL -1)
        list(APPEND failures "--plot output has no %%EOF trailer")
    endif()
else()
    list(APPEND failures "--plot did not create ${pdf}")
endif()

# Scale, window and monochrome all reach the shared renderer.
run(0 plot_scale --plot "${good}" "${WORK}/cli_scale.pdf" --scale 1:5)
run(0 plot_win --plot "${good}" "${WORK}/cli_win.pdf" --window 0,0,50,25 --monochrome)
run(0 plot_a3 --plot "${good}" "${WORK}/cli_a3.pdf" --paper A3 --landscape)

# --- 6: plot failure modes are distinguishable by exit code ------------------------
run(1 plot_bad_paper --plot "${good}" "${WORK}/x.pdf" --paper A9)          # usage
run(2 plot_bad_input --plot "${WORK}/absent.musa" "${WORK}/x.pdf")         # load
run(1 plot_no_output --plot "${good}")                                     # usage

# --- 7: --dump-wiki-data writes the documentation's JSON, with no display ------------
set(wiki "${WORK}/wiki-data.json")
file(REMOVE "${wiki}")
run(0 wiki_out --dump-wiki-data "${wiki}")
if(EXISTS "${wiki}")
    file(READ "${wiki}" wiki_json)
    foreach(key "\"commands\"" "\"ribbon\"" "\"shortcuts\"" "\"formatVersion\"" "\"LINE\"")
        string(FIND "${wiki_json}" "${key}" at)
        if(at EQUAL -1)
            list(APPEND failures "--dump-wiki-data output has no ${key}")
        endif()
    endforeach()
else()
    list(APPEND failures "--dump-wiki-data did not create ${wiki}")
endif()
run(1 wiki_no_path --dump-wiki-data)                                        # usage

if(failures)
    string(REPLACE ";" "\n  " pretty "${failures}")
    message(FATAL_ERROR "CLI end-to-end check failed:\n  ${pretty}")
endif()
message(STATUS "cli_check: help/version/check exit codes correct with no display")
