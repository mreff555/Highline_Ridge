cmake_minimum_required(VERSION 3.24)
# Highline Ridge — showcase game on the Timberline narrative engine.
project(HighlineRidge CXX)
set(CMAKE_BUILD_TYPE Release)

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
    src/SubSceneDef.cpp
    src/MovementMappingDef.cpp
    src/MaskEvaluator.cpp
    src/MovementResolver.cpp
    src/SceneInventory.cpp
    src/SceneOverlayDef.cpp
    src/SceneOverlayMgr.cpp
    src/ConversationManager.cpp
    src/ConversationRequirements.cpp
    src/BlackjackGame.cpp
    src/BlackjackPanel.cpp
    src/MilestoneDatabase.cpp
    src/MilestoneManager.cpp
    src/InventoryMgr.cpp
    src/TakeMgr.cpp
    src/InteractionMgr.cpp
    src/SpeakTargetMgr.cpp
    src/ImageCompression.cpp
    src/AssetStore.cpp
    src/JobSystem.cpp
    src/SimdUtil.cpp
    src/DisplayAspect.cpp
    src/PlatformPath.cpp
    src/GameConfig.cpp
    src/AudioManager.cpp
    src/UiBackdrop.cpp
    src/OpusAudio.cpp
    src/PauseMenuMgr.cpp
    src/MainMenuBackdrop.cpp
    src/SaveLoadMenuMgr.cpp
    src/DropConfirmMgr.cpp
    src/SaveGame.cpp
    src/TextDigest.cpp
    src/TtsVoiceMarkup.cpp
    src/TtsContentValidator.cpp
    src/XaiTtsClient.cpp
    src/ItemInstance.cpp
    src/ItemDatabase.cpp
    src/ItemCombinationDatabase.cpp
)

include(FetchContent)

# Bundle raylib 5.5 for reliable miniaudio/CoreAudio output.
# Homebrew raylib 5.0-dev can initialize with backend "Null" (no audible output on macOS).
option(HIGHLINE_USE_SYSTEM_RAYLIB "Use system raylib instead of bundled 5.5" OFF)
# 0 = auto (hardware_concurrency - 1). Applies when code uses JobSystem(workerOverride).
set(HIGHLINE_JOB_WORKERS "0" CACHE STRING "JobSystem worker count (0=auto)")
option(HIGHLINE_SIMD "Enable NEON/SSE-tuned simdFillBytes (downscale always built)" ON)
# Dev default: disk resources + scene editor.
# Release: -DHIGHLINE_RELEASE=ON → embed ON; editor stays OFF unless you pass
# -DHIGHLINE_BUILD_EDITOR=ON explicitly (RELEASE no longer FORCE-overwrites it).
option(HIGHLINE_RELEASE "Release mode: embed resources (editor off unless overridden)" OFF)
if(HIGHLINE_RELEASE)
    set(_highline_embed_default ON)
    set(_highline_editor_default OFF)
else()
    set(_highline_embed_default OFF)
    set(_highline_editor_default ON)
endif()
option(HIGHLINE_EMBED_RESOURCES "Pack resources into the game binary" ${_highline_embed_default})
option(HIGHLINE_BUILD_EDITOR "Build the scene editor (ON for normal/dev builds)" ${_highline_editor_default})
# In-game developer tools (Ctrl+Shift+S overlay, ~ console). ON for normal/dev builds;
# OFF under HIGHLINE_RELEASE unless -DHIGHLINE_DEV_TOOLS=ON / --with-dev-tools.
if(HIGHLINE_RELEASE)
    set(_highline_dev_tools_default OFF)
else()
    set(_highline_dev_tools_default ON)
endif()
option(HIGHLINE_DEV_TOOLS "Enable in-game developer tools" ${_highline_dev_tools_default})
option(HIGHLINE_INSTALL_LINKS "Install /usr/local/bin symlinks (Linux/macOS)" ON)

if(HIGHLINE_RELEASE)
    # Always embed for release; do not FORCE editor off so -DHIGHLINE_BUILD_EDITOR=ON works.
    set(HIGHLINE_EMBED_RESOURCES ON CACHE BOOL "Pack resources into the game binary" FORCE)
    message(STATUS "HIGHLINE_RELEASE=ON → EMBED=ON, BUILD_EDITOR=${HIGHLINE_BUILD_EDITOR}, DEV_TOOLS=${HIGHLINE_DEV_TOOLS}")
    if(NOT HIGHLINE_BUILD_EDITOR)
        message(STATUS "  Tip: pass -DHIGHLINE_BUILD_EDITOR=ON to also build ./scene-editor")
    endif()
    if(NOT HIGHLINE_DEV_TOOLS)
        message(STATUS "  Tip: pass -DHIGHLINE_DEV_TOOLS=ON (or ./build-release.sh --with-dev-tools) for Ctrl+Shift+S / ~ console")
    endif()
endif()

if(UNIX AND NOT APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # Documented release default; users can still override -DCMAKE_INSTALL_PREFIX=...
endif()
if(UNIX AND NOT APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/highline_ridge" CACHE PATH "Install prefix" FORCE)
endif()

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
if(HIGHLINE_DEV_TOOLS)
    list(APPEND SOURCES src/DevConsole.cpp)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Highline Ridge")
if(HIGHLINE_RELEASE)
    # Routes TraceLog to stderr (see GameApplication::releaseTraceLog).
    target_compile_definitions(${PROJECT_NAME} PRIVATE HIGHLINE_RELEASE=1)
endif()
if(HIGHLINE_DEV_TOOLS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HIGHLINE_DEV_TOOLS=1)
    message(STATUS "HIGHLINE_DEV_TOOLS=ON — Ctrl+Shift+S overlay and ~ developer console enabled")
else()
    message(STATUS "HIGHLINE_DEV_TOOLS=OFF — in-game developer tools stripped")
endif()

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
#    (skipped for embedded release builds once Phase 2 lands).
set(_gen_dir "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_gen_dir}")

if(NOT HIGHLINE_EMBED_RESOURCES)
    add_custom_target(sync_resources ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "${CMAKE_BINARY_DIR}/resources"
        COMMENT "Syncing resources to build directory"
    )
    add_dependencies(${PROJECT_NAME} sync_resources)
else()
    message(STATUS "HIGHLINE_EMBED_RESOURCES=ON — packing resources into binary")
    enable_language(ASM)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_pak "${_gen_dir}/highline_assets.pak")
    set(_pak_S "${_gen_dir}/highline_assets_pak.S")
    set(_pak_cpp "${_gen_dir}/EmbeddedAssets.cpp")
    set(_pak_h "${_gen_dir}/EmbeddedAssets.h")

    add_custom_command(
        OUTPUT "${_pak}" "${_pak_S}" "${_pak_cpp}" "${_pak_h}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/pack_assets.py"
            --resources "${CMAKE_SOURCE_DIR}/resources"
            --out-dir "${_gen_dir}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/pack_assets.py"
            "${CMAKE_SOURCE_DIR}/resources"
        COMMENT "Packing resources into highline_assets.pak + embed asm"
        VERBATIM
    )
    add_custom_target(pack_highline_assets DEPENDS "${_pak}" "${_pak_S}" "${_pak_cpp}")

    set_source_files_properties("${_pak_S}" PROPERTIES
        COMPILE_FLAGS "-I${_gen_dir}"
        LANGUAGE ASM
    )
    target_sources(${PROJECT_NAME} PRIVATE "${_pak_cpp}" "${_pak_S}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${_gen_dir}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HIGHLINE_EMBED_RESOURCES=1)
    add_dependencies(${PROJECT_NAME} pack_highline_assets)

    # Also drop the .pak beside the binary as a fallback / inspection aid.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_pak}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/highline_assets.pak"
        COMMENT "Copying highline_assets.pak beside executable"
    )
endif()

if(HIGHLINE_BUILD_EDITOR)
    message(STATUS "HIGHLINE_BUILD_EDITOR=ON — building tools/scene-editor")
    add_subdirectory(tools/scene-editor)
else()
    message(STATUS "HIGHLINE_BUILD_EDITOR=OFF — skipping scene editor")
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# 8. Hard-fail the build when TTS policy / markup is invalid (same rules as load/refresh).
add_executable(validate_tts tools/validate_tts.cpp
    src/TtsVoiceMarkup.cpp
    src/TtsContentValidator.cpp
)
target_include_directories(validate_tts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(validate_tts PRIVATE nlohmann_json::nlohmann_json raylib)
if(APPLE)
    target_link_libraries(validate_tts PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
endif()

add_executable(test_job_system tools/test_job_system.cpp src/JobSystem.cpp)
target_include_directories(test_job_system PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_job_system PRIVATE Threads::Threads)

add_custom_target(check_tts_resources ALL
    COMMAND validate_tts
        "${CMAKE_SOURCE_DIR}/resources/scenes.json"
        "${CMAKE_SOURCE_DIR}/resources/conversations.json"
        "${CMAKE_SOURCE_DIR}/resources/items.json"
        ""
    DEPENDS validate_tts
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Validating TTS scene/item policies and voice markup"
)
add_dependencies(${PROJECT_NAME} check_tts_resources)
if(TARGET sync_resources)
    add_dependencies(${PROJECT_NAME} sync_resources)
endif()
