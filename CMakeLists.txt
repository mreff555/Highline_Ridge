cmake_minimum_required(VERSION 3.24)
project(HighlineRidge CXX)

# C++17 for std::filesystem (cross-platform paths and save directories).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SOURCES
    src/main.cpp
    src/GameApplication.cpp
    src/GameSession.cpp
    src/ScrollPanel.cpp
    src/ModalPanel.cpp
    src/PlayerStats.cpp
    src/GameContext.cpp
    src/SceneVisitTracker.cpp
    src/WorldState.cpp
    src/SaveGameService.cpp
    src/UiCoordinator.cpp
    src/ActiveScene.cpp
    src/SceneController.cpp
    src/NarrativeNotebook.cpp
    src/SidePanel.cpp
    src/ProgressionService.cpp
    src/Button.cpp
    src/ButtonMgr.cpp
    src/SceneLoader.cpp
    src/SceneOverlayDef.cpp
    src/SceneOverlayMgr.cpp
    src/ConversationManager.cpp
    src/MilestoneDatabase.cpp
    src/MilestoneManager.cpp
    src/InventoryMgr.cpp
    src/TakeMgr.cpp
    src/InteractionMgr.cpp
    src/ImageCompression.cpp
    src/PlatformPath.cpp
    src/GameConfig.cpp
    src/AudioManager.cpp
    src/UiBackdrop.cpp
    src/OpusAudio.cpp
    src/PauseMenuMgr.cpp
    src/SaveLoadMenuMgr.cpp
    src/DropConfirmMgr.cpp
    src/SaveGame.cpp
    src/XaiTtsClient.cpp
    src/ItemInstance.cpp
    src/ItemDatabase.cpp
    src/ItemCombinationDatabase.cpp
)

include(FetchContent)

# Bundle raylib 5.5 for reliable miniaudio/CoreAudio output.
# Homebrew raylib 5.0-dev can initialize with backend "Null" (no audible output on macOS).
option(HIGHLINE_USE_SYSTEM_RAYLIB "Use system raylib instead of bundled 5.5" OFF)

set(_raylib_ready FALSE)
if(HIGHLINE_USE_SYSTEM_RAYLIB)
    find_package(raylib QUIET)
    if(raylib_FOUND)
        if(raylib_INCLUDE_DIR)
            file(READ "${raylib_INCLUDE_DIR}/raylib.h" _raylib_header)
        elseif(raylib_INCLUDE_DIRS)
            list(GET raylib_INCLUDE_DIRS 0 _raylib_include_path)
            file(READ "${_raylib_include_path}/raylib.h" _raylib_header)
        else()
            set(_raylib_header "")
        endif()

        if(_raylib_header MATCHES "#define RAYLIB_VERSION_MAJOR ([0-9]+)")
            set(_raylib_version_major "${CMAKE_MATCH_1}")
        endif()
        if(_raylib_header MATCHES "#define RAYLIB_VERSION_MINOR ([0-9]+)")
            set(_raylib_version_minor "${CMAKE_MATCH_1}")
        endif()

        if(_raylib_version_major LESS 5 OR
           (_raylib_version_major EQUAL 5 AND _raylib_version_minor LESS 5))
            message(FATAL_ERROR
                "System raylib ${_raylib_version_major}.${_raylib_version_minor} is too old; "
                "Highline Ridge requires raylib 5.5+ for audio. "
                "Reconfigure with -DHIGHLINE_USE_SYSTEM_RAYLIB=OFF (default) to use bundled raylib.")
        endif()

        set(_raylib_ready TRUE)
        message(STATUS "Using system raylib ${_raylib_version_major}.${_raylib_version_minor}")
    endif()
endif()

if(NOT _raylib_ready)
    message(STATUS "Using bundled raylib 5.5 (FetchContent)")

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
    pkg_check_modules(OPUSFILE IMPORTED_TARGET opusfile)
endif()

if(NOT LIBLZMA_FOUND)
    find_library(LZMA_LIBRARY NAMES lzma liblzma lzma.lib)
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

if(NOT LIBLZMA_FOUND AND NOT TARGET LZMA::LZMA)
    message(FATAL_ERROR "liblzma (xz) is required for compressed image assets")
endif()

if(NOT LIBJPEG_FOUND)
    find_library(JPEG_LIBRARY NAMES jpeg jpeg.lib turbojpeg turbojpeg.lib)
    find_path(JPEG_INCLUDE_DIR jpeglib.h)
    if(JPEG_LIBRARY AND JPEG_INCLUDE_DIR)
        add_library(JPEG::JPEG UNKNOWN IMPORTED)
        set_target_properties(JPEG::JPEG PROPERTIES
            IMPORTED_LOCATION "${JPEG_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INCLUDE_DIR}"
        )
        set(LIBJPEG JPEG::JPEG)
    else()
        message(FATAL_ERROR "libjpeg is required for JPEG image assets")
    endif()
endif()

if(NOT OPUSFILE_FOUND)
    find_library(OPUSFILE_LIBRARY NAMES opusfile opusfile.lib)
    find_library(OPUS_LIBRARY NAMES opus opus.lib)
    find_path(OPUSFILE_INCLUDE_DIR opusfile.h)
    if(OPUSFILE_LIBRARY AND OPUS_LIBRARY AND OPUSFILE_INCLUDE_DIR)
        add_library(OPUSFILE::OPUSFILE UNKNOWN IMPORTED)
        set_target_properties(OPUSFILE::OPUSFILE PROPERTIES
            IMPORTED_LOCATION "${OPUSFILE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPUSFILE_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${OPUS_LIBRARY}"
        )
        set(OPUSFILE OPUSFILE::OPUSFILE)
    else()
        message(FATAL_ERROR "libopusfile and libopus are required for dialog .opus audio")
    endif()
endif()

# 3. Create the executable (use ${PROJECT_NAME} so everything stays consistent)
add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Highline Ridge")

# 4. Include your own headers (src/ folder)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 5. Link raylib
set(_image_deps nlohmann_json::nlohmann_json)
if(TARGET PkgConfig::LIBLZMA)
    list(APPEND _image_deps PkgConfig::LIBLZMA)
else()
    list(APPEND _image_deps ${LIBLZMA})
endif()
if(TARGET PkgConfig::LIBJPEG)
    list(APPEND _image_deps PkgConfig::LIBJPEG)
else()
    list(APPEND _image_deps ${LIBJPEG})
endif()

set(_audio_deps)
if(TARGET PkgConfig::OPUSFILE)
    list(APPEND _audio_deps PkgConfig::OPUSFILE)
else()
    list(APPEND _audio_deps ${OPUSFILE})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE raylib ${_image_deps} ${_audio_deps})

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
endif()

# 6. macOS frameworks (required by raylib on Apple platforms)
if (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
endif()

# 7. Keep resources beside the executable so texture loads work when launched from build/
add_custom_target(sync_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "${CMAKE_BINARY_DIR}/resources"
    COMMENT "Syncing resources to build directory"
)
add_dependencies(${PROJECT_NAME} sync_resources)
