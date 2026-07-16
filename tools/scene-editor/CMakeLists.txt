cmake_minimum_required(VERSION 3.24)
project(HighlineRidgeSceneEditor CXX)

# Match the game target: ImageCompression uses std::filesystem.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

option(HIGHLINE_USE_SYSTEM_RAYLIB "Use system raylib instead of bundled 5.5" OFF)

set(_raylib_ready FALSE)
if(HIGHLINE_USE_SYSTEM_RAYLIB)
    find_package(raylib QUIET)
    if(raylib_FOUND)
        set(_raylib_ready TRUE)
    endif()
endif()

if(NOT _raylib_ready)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(raylib)

    if(TARGET raylib)
        target_compile_options(raylib PRIVATE -Wno-tautological-compare)
    endif()
endif()

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma)
    pkg_check_modules(LIBJPEG IMPORTED_TARGET libjpeg)
endif()

if(NOT LIBLZMA_FOUND)
    find_library(LZMA_LIBRARY NAMES lzma)
    find_path(LZMA_INCLUDE_DIR lzma.h)
    if(LZMA_LIBRARY AND LZMA_INCLUDE_DIR)
        add_library(LZMA::LZMA UNKNOWN IMPORTED)
        set_target_properties(LZMA::LZMA PROPERTIES
            IMPORTED_LOCATION "${LZMA_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LZMA_INCLUDE_DIR}"
        )
        set(LIBLZMA LZMA::LZMA)
    endif()
endif()

if(NOT LIBJPEG_FOUND)
    find_library(JPEG_LIBRARY NAMES jpeg)
    find_path(JPEG_INCLUDE_DIR jpeglib.h)
    if(JPEG_LIBRARY AND JPEG_INCLUDE_DIR)
        add_library(JPEG::JPEG UNKNOWN IMPORTED)
        set_target_properties(JPEG::JPEG PROPERTIES
            IMPORTED_LOCATION "${JPEG_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INCLUDE_DIR}"
        )
        set(LIBJPEG JPEG::JPEG)
    endif()
endif()

set(EDITOR_SOURCES
    main.cpp
    ${CMAKE_SOURCE_DIR}/../../src/SceneDocument.cpp
    ${CMAKE_SOURCE_DIR}/../../src/ImageCompression.cpp
    ${CMAKE_SOURCE_DIR}/../../src/PlatformPath.cpp
)

add_executable(scene-editor ${EDITOR_SOURCES})

target_include_directories(scene-editor PRIVATE
    ${CMAKE_SOURCE_DIR}/../../src
)

set(_image_deps nlohmann_json::nlohmann_json)
if(TARGET PkgConfig::LIBLZMA)
    list(APPEND _image_deps PkgConfig::LIBLZMA)
elseif(TARGET LZMA::LZMA)
    list(APPEND _image_deps LZMA::LZMA)
endif()
if(TARGET PkgConfig::LIBJPEG)
    list(APPEND _image_deps PkgConfig::LIBJPEG)
elseif(TARGET JPEG::JPEG)
    list(APPEND _image_deps JPEG::JPEG)
endif()

target_link_libraries(scene-editor PRIVATE raylib ${_image_deps})

if(APPLE)
    target_link_libraries(scene-editor PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreFoundation"
    )
endif()