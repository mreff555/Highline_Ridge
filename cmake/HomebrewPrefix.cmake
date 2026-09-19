# Homebrew on Apple Silicon installs under /opt/homebrew. GUI IDEs, CI shells,
# and some terminals never put that on PATH / PKG_CONFIG_PATH / CMAKE_PREFIX_PATH,
# so pkg-config and find_library miss libjpeg, liblzma, opusfile, etc.
#
# Call once before find_package(PkgConfig) / pkg_check_modules / find_library.

if(NOT APPLE)
    return()
endif()

if(DEFINED HIGHLINE_HOMEBREW_PREFIX AND HIGHLINE_HOMEBREW_PREFIX)
    # Already resolved (or user-forced via -DHIGHLINE_HOMEBREW_PREFIX=...).
else()
    set(_highline_brew "")
    find_program(_highline_brew brew
        PATHS /opt/homebrew/bin /usr/local/bin
        NO_DEFAULT_PATH)
    if(NOT _highline_brew)
        find_program(_highline_brew brew)
    endif()

    if(_highline_brew)
        execute_process(
            COMMAND "${_highline_brew}" --prefix
            OUTPUT_VARIABLE HIGHLINE_HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _highline_brew_rc)
        if(NOT _highline_brew_rc EQUAL 0)
            set(HIGHLINE_HOMEBREW_PREFIX "")
        endif()
    endif()

    if(NOT HIGHLINE_HOMEBREW_PREFIX)
        if(EXISTS "/opt/homebrew/opt")
            set(HIGHLINE_HOMEBREW_PREFIX "/opt/homebrew")
        elseif(EXISTS "/usr/local/opt")
            set(HIGHLINE_HOMEBREW_PREFIX "/usr/local")
        endif()
    endif()

    set(HIGHLINE_HOMEBREW_PREFIX "${HIGHLINE_HOMEBREW_PREFIX}" CACHE PATH
        "Homebrew prefix (Apple Silicon usually /opt/homebrew)" FORCE)
endif()

if(NOT HIGHLINE_HOMEBREW_PREFIX)
    return()
endif()

list(PREPEND CMAKE_PREFIX_PATH "${HIGHLINE_HOMEBREW_PREFIX}")
list(PREPEND CMAKE_PROGRAM_PATH "${HIGHLINE_HOMEBREW_PREFIX}/bin")
list(PREPEND CMAKE_LIBRARY_PATH
    "${HIGHLINE_HOMEBREW_PREFIX}/lib"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/jpeg/lib"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/jpeg-turbo/lib"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/xz/lib"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/opusfile/lib"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/opus/lib")
list(PREPEND CMAKE_INCLUDE_PATH
    "${HIGHLINE_HOMEBREW_PREFIX}/include"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/jpeg/include"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/jpeg-turbo/include"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/xz/include"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/opusfile/include"
    "${HIGHLINE_HOMEBREW_PREFIX}/opt/opus/include")

set(_highline_pc "${HIGHLINE_HOMEBREW_PREFIX}/lib/pkgconfig")
if(EXISTS "${_highline_pc}")
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
        set(ENV{PKG_CONFIG_PATH} "${_highline_pc}:$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${_highline_pc}")
    endif()
endif()

# Also expose keg-only pkgconfig dirs Homebrew commonly uses.
foreach(_keg jpeg jpeg-turbo xz opusfile opus)
    set(_keg_pc "${HIGHLINE_HOMEBREW_PREFIX}/opt/${_keg}/lib/pkgconfig")
    if(EXISTS "${_keg_pc}")
        set(ENV{PKG_CONFIG_PATH} "${_keg_pc}:$ENV{PKG_CONFIG_PATH}")
    endif()
endforeach()

message(STATUS "Homebrew prefix for deps: ${HIGHLINE_HOMEBREW_PREFIX}")
