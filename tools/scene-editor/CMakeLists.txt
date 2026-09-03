cmake_minimum_required(VERSION 3.24)

# Standalone: cmake -S tools/scene-editor -B tools/scene-editor/build
# Nested:     enabled from repo root via HIGHLINE_BUILD_EDITOR (dev default ON)

set(_EDITOR_STANDALONE TRUE)
if(TARGET raylib AND TARGET nlohmann_json::nlohmann_json)
    set(_EDITOR_STANDALONE FALSE)
endif()

if(_EDITOR_STANDALONE)
    project(TimberlineResourceEditor CXX)
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

    set(_ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    set(_RESOURCES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../resources")
else()
    # Nested under HighlineRidge: reuse parent raylib/json/deps.
    set(_ENGINE_SRC "${CMAKE_SOURCE_DIR}/src")
    set(_RESOURCES_SRC "${CMAKE_SOURCE_DIR}/resources")
    # Parent already found LIBLZMA / LIBJPEG into these variables or imported targets.
endif()

set(EDITOR_SOURCES
    main.cpp
    EditorPaths.cpp
    EditorPrefs.cpp
    EditorInput.cpp
    EditorUiDraw.cpp
    EditorButton.cpp
    ConversationHelpers.cpp
    ThumbnailCache.cpp
    EditorLayout.cpp
    DocumentWorkspace.cpp
    VariableEditor.cpp
    ConversationTree.cpp
    DialogWalkthrough.cpp
    ItemAuthoring.cpp
    ItemEditor.cpp
    ItemEditorAuthoring.cpp
    SceneAuthoring.cpp
    SceneAuthoringDialog.cpp
    SceneAssistDialog.cpp
    SceneInventoryDialog.cpp
    SceneEffectsDialog.cpp
    SceneTransitionDialog.cpp
    EditorPreferencesDialog.cpp
    SceneGraphModel.cpp
    SceneMapCanvas.cpp
    SceneEditorApp.cpp
    ${_ENGINE_SRC}/SceneDocument.cpp
    ${_ENGINE_SRC}/ImageCompression.cpp
    ${_ENGINE_SRC}/JobSystem.cpp
    ${_ENGINE_SRC}/SimdUtil.cpp
    ${_ENGINE_SRC}/PlatformPath.cpp
    ${_ENGINE_SRC}/TtsVoiceMarkup.cpp
    ${_ENGINE_SRC}/AssetStore.cpp
)

if(APPLE)
    list(APPEND EDITOR_SOURCES
        macos/EditorNativeMenu.mm
        macos/EditorInput_macos.mm
    )
endif()

add_executable(scene-editor ${EDITOR_SOURCES})

if(APPLE)
    # Compile as Objective-C++ without requiring project-level OBJCXX language.
    set_source_files_properties(
        macos/EditorNativeMenu.mm
        macos/EditorInput_macos.mm
        PROPERTIES
            COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
            LANGUAGE CXX
    )
endif()

target_include_directories(scene-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_ENGINE_SRC}
)

set(_image_deps nlohmann_json::nlohmann_json)
if(TARGET PkgConfig::LIBLZMA)
    list(APPEND _image_deps PkgConfig::LIBLZMA)
elseif(TARGET LZMA::LZMA)
    list(APPEND _image_deps LZMA::LZMA)
elseif(DEFINED LIBLZMA)
    list(APPEND _image_deps ${LIBLZMA})
endif()
if(TARGET PkgConfig::LIBJPEG)
    list(APPEND _image_deps PkgConfig::LIBJPEG)
elseif(TARGET JPEG::JPEG)
    list(APPEND _image_deps JPEG::JPEG)
elseif(DEFINED LIBJPEG)
    list(APPEND _image_deps ${LIBJPEG})
endif()

find_package(Threads REQUIRED)
target_link_libraries(scene-editor PRIVATE raylib ${_image_deps} Threads::Threads)

# Nested build: put scene-editor next to Highline Ridge in the top build dir.
if(NOT _EDITOR_STANDALONE)
    set_target_properties(scene-editor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

# Resources beside the editor binary (symlink when nested; same for standalone).
if(NOT _EDITOR_STANDALONE AND TARGET sync_resources)
    # Game already syncs ${CMAKE_BINARY_DIR}/resources — reuse it.
    add_dependencies(scene-editor sync_resources)
else()
    set(_editor_resources_link_dir "${CMAKE_CURRENT_BINARY_DIR}")
    if(NOT _EDITOR_STANDALONE)
        set(_editor_resources_link_dir "${CMAKE_BINARY_DIR}")
    endif()
    add_custom_target(sync_scene_editor_resources ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_editor_resources_link_dir}/resources"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${_RESOURCES_SRC}"
            "${_editor_resources_link_dir}/resources"
        COMMENT "Linking resources beside scene-editor"
    )
    add_dependencies(scene-editor sync_scene_editor_resources)
endif()

if(APPLE)
    target_link_libraries(scene-editor PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreFoundation"
    )
endif()
