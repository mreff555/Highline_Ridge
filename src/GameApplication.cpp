/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#include "GameApplication.h"

#include <AssetStore.h>
#include <GameConfig.h>
#include <ImageCompression.h>
#include <LocationStruct.h>
#include <PlatformPath.h>
#include <TtsContentValidator.h>
#include <XaiTtsClient.h>
#include <raylib.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

#if defined(HIGHLINE_EMBED_RESOURCES)
#include "EmbeddedAssets.h"
#endif

namespace timberline_engine
{

namespace
{

#if defined(HIGHLINE_RELEASE)
// Dev builds keep raylib's default TraceLog → stdout. Release routes the same
// prefixed lines to stderr so stdout stays clean for pipes/launchers.
void releaseTraceLog(int logType, const char* text, va_list args)
{
    char buffer[256] = {0};
    switch (logType)
    {
        case LOG_TRACE:
            std::strcpy(buffer, "TRACE: ");
            break;
        case LOG_DEBUG:
            std::strcpy(buffer, "DEBUG: ");
            break;
        case LOG_INFO:
            std::strcpy(buffer, "INFO: ");
            break;
        case LOG_WARNING:
            std::strcpy(buffer, "WARNING: ");
            break;
        case LOG_ERROR:
            std::strcpy(buffer, "ERROR: ");
            break;
        case LOG_FATAL:
            std::strcpy(buffer, "FATAL: ");
            break;
        default:
            break;
    }

    const auto textSize = static_cast<unsigned>(std::strlen(text));
    const auto prefixLen = std::strlen(buffer);
    const unsigned copyLen =
        (textSize < (sizeof(buffer) - prefixLen - 2u)) ? textSize
                                                      : static_cast<unsigned>(sizeof(buffer) - prefixLen - 2u);
    std::memcpy(buffer + prefixLen, text, copyLen);
    buffer[prefixLen + copyLen] = '\n';
    buffer[prefixLen + copyLen + 1] = '\0';

    std::vfprintf(stderr, buffer, args);
    std::fflush(stderr);

    if (logType == LOG_FATAL)
        std::exit(EXIT_FAILURE);
}
#endif

struct CommandLineOptions
{
    bool showHelp = false;
    bool forceRefreshVoices = false;
    bool refreshAllVoices = false;
    std::string apiKey;
    std::string refreshFilter;
};

bool extractPrefixedValue(const std::string& argument, const std::string& prefix, std::string& out)
{
    if (argument.compare(0, prefix.size(), prefix) != 0)
        return false;

    out = argument.substr(prefix.size());
    return true;
}

CommandLineOptions parseCommandLine(int argc, char* argv[])
{
    CommandLineOptions options;

    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        if (argument == "-h" || argument == "--help")
        {
            options.showHelp = true;
            continue;
        }

        if (argument == "-force" || argument == "--force")
        {
            options.forceRefreshVoices = true;
            continue;
        }

        if (argument == "--refresh-voices")
        {
            options.refreshAllVoices = true;
            continue;
        }

        std::string prefixedValue;
        if (extractPrefixedValue(argument, "--key=", prefixedValue))
        {
            options.apiKey = prefixedValue;
            continue;
        }

        if (extractPrefixedValue(argument, "--refresh=", prefixedValue))
        {
            options.refreshFilter = prefixedValue;
            continue;
        }

        const std::string legacyRefreshPrefix = "--refresh-voices=";
        if (extractPrefixedValue(argument, legacyRefreshPrefix, prefixedValue))
        {
            std::cerr << "Warning: --refresh-voices=API_KEY is deprecated. Use:\n"
                      << "  --key=" << prefixedValue << " --refresh-voices\n";
            options.apiKey = prefixedValue;
            options.refreshAllVoices = true;
            continue;
        }

        std::cerr << "Unknown option: " << argument << "\n";
        options.showHelp = true;
    }

    if (options.refreshAllVoices && !options.refreshFilter.empty())
    {
        std::cerr << "Cannot combine --refresh-voices with --refresh=ID\n";
        options.showHelp = true;
    }

    return options;
}

std::string resourcePathForRefreshRead(const char* relativePath)
{
    const std::string fromSource = pathJoin("..", relativePath);
    if (FileExists(fromSource.c_str()))
        return fromSource;

    return relativePath;
}

}

bool GameApplication::locateGameResources()
{
    auto tryDirectory = [](const std::string& directory) -> bool
    {
        if (directory.empty())
            return false;

        const std::string scenesPath = pathJoin(directory, "resources/scenes.json");
        if (!FileExists(scenesPath.c_str()))
            return false;

        ChangeDirectory(directory.c_str());
        TraceLog(LOG_INFO, "Using game resources from: %s", directory.c_str());
        return true;
    };

    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0')
    {
        const std::string executableDirectory(appDir);
        if (tryDirectory(executableDirectory))
            return true;

        const std::string parentDirectory = pathJoin(executableDirectory, "..");
        if (tryDirectory(parentDirectory))
            return true;
    }

    if (tryDirectory("."))
        return true;

    if (tryDirectory(".."))
        return true;

    return false;
}

bool GameApplication::initializeWindow(const GameConfig& config)
{
    InitWindow(config.display.width, config.display.height, "Highline Ridge");
    SetExitKey(0);
    if (config.display.fullscreen)
        ToggleFullscreen();
    else
        applySavedWindowPlacement(config.display);
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    return true;
}

bool GameApplication::loadDatabases()
{
    std::string scenesPath = "resources/scenes.json";
    std::string conversationsPath = "resources/conversations.json";
    std::string itemsPath = "resources/items.json";

    const bool scenesLoaded =
        sceneDatabase.load(scenesPath, ".") ||
        sceneDatabase.load("../resources/scenes.json", "..");
    if (!scenesLoaded)
    {
        TraceLog(LOG_ERROR, "Failed to load scenes from resources/scenes.json");
        return false;
    }
    if (!FileExists(scenesPath.c_str()) && FileExists("../resources/scenes.json"))
    {
        scenesPath = "../resources/scenes.json";
        conversationsPath = "../resources/conversations.json";
        itemsPath = "../resources/items.json";
    }

    const bool milestonesLoaded =
        milestoneDatabase.load("resources/milestones.json") ||
        milestoneDatabase.load("../resources/milestones.json");
    if (!milestonesLoaded)
        TraceLog(LOG_WARNING, "Failed to load milestones from resources/milestones.json");

    const bool itemsLoaded =
        itemDatabase.load("resources/items.json", ".") ||
        itemDatabase.load("../resources/items.json", "..");
    if (!itemsLoaded)
        TraceLog(LOG_WARNING, "Failed to load items from resources/items.json");

    // Craft recipes live on product items (components); no separate combinations file.
    if (!validateTtsResourcesOrLog(scenesPath, conversationsPath, itemsPath, ""))
    {
        TraceLog(LOG_ERROR, "Aborting load: TTS resource validation failed");
        return false;
    }

    return true;
}

void GameApplication::shutdown()
{
    if (session)
        session->persistDisplayConfig();
    session.reset();
    audioManager.shutdown();
    CloseWindow();
}

int GameApplication::run(int argc, char* argv[])
{
#if defined(HIGHLINE_RELEASE)
    SetTraceLogCallback(releaseTraceLog);
#endif

    const CommandLineOptions commandLine = parseCommandLine(argc, argv);
    if (commandLine.showHelp)
    {
        printGameHelp(argc > 0 ? argv[0] : nullptr);
        return 0;
    }

#if defined(HIGHLINE_EMBED_RESOURCES)
    {
        std::string pakError;
        bool pakOk = false;
#if defined(__APPLE__)
        // Mach-O: C symbols are underscored in the object file; the extern names
        // in EmbeddedAssets.h are the C names (compiler adds _).
#endif
        pakOk = PakAssetStore::openMemory(
            embeddedPakBytes(), embeddedPakSize(), pakAssets, pakError);
        if (!pakOk)
        {
            // Fallback: highline_assets.pak beside the executable.
            const char* appDir = GetApplicationDirectory();
            const std::string pakPath = pathJoin(
                appDir != nullptr ? appDir : ".", kEmbeddedAssetPakFile);
            pakOk = PakAssetStore::openFile(pakPath, pakAssets, pakError);
        }
        if (!pakOk)
        {
            TraceLog(LOG_ERROR, "Failed to open embedded asset pak: %s", pakError.c_str());
            std::cerr << "Failed to open embedded assets: " << pakError << "\n";
            return 1;
        }
        setAssets(&pakAssets);
        usingPakAssets = true;
        TraceLog(
            LOG_INFO,
            "Using embedded asset pak (%zu entries)",
            pakAssets.entryCount());

        // Still try to park CWD next to the binary for any relative fallbacks.
        if (const char* appDir = GetApplicationDirectory();
            appDir != nullptr && appDir[0] != '\0')
            ChangeDirectory(appDir);
    }
#else
    if (!locateGameResources())
    {
        TraceLog(LOG_ERROR, "Could not locate resources/scenes.json next to executable or working directory");
        std::cerr << "Could not find game resources (resources/scenes.json).\n"
                  << "Run the game from the build directory, e.g. ./build/Highline\\ Ridge\n"
                  << "or rebuild so resources are synced: cmake --build build\n";
        if (commandLine.refreshAllVoices || !commandLine.refreshFilter.empty())
            std::cerr << "Cannot refresh voices without game resources.\n";
        return 1;
    }

    diskAssets = DiskAssetStore(".");
    setAssets(&diskAssets);
    usingPakAssets = false;
#endif

    TraceLog(LOG_INFO, "User data directory: %s", userDataRoot().c_str());

#if defined(HIGHLINE_EMBED_RESOURCES)
    // Defaults from pack; user overrides live in user_config.json (Phase 2.1).
    {
        std::string configText;
        if (assets().readText("resources/game_config.json", configText))
        {
            // loadGameConfig still wants a path — write a temp default into user
            // data only if missing, else keep disk API for now.
            const std::string userCfg = userConfigPath();
            if (!FileExists(userCfg.c_str()))
            {
                ensureParentDirectories(userCfg);
                writeBinaryFile(
                    userCfg,
                    reinterpret_cast<const unsigned char*>(configText.data()),
                    configText.size());
            }
            gameConfigPath = userCfg;
        }
    }
#endif

    if (!loadGameConfig(gameConfigPath, gameConfig))
    {
#if !defined(HIGHLINE_EMBED_RESOURCES)
        if (loadGameConfig("../resources/game_config.json", gameConfig))
            gameConfigPath = "../resources/game_config.json";
        else
#endif
            TraceLog(LOG_WARNING, "Failed to load game config; using defaults");
    }

    const bool refreshRequested =
        commandLine.refreshAllVoices || !commandLine.refreshFilter.empty();
    if (refreshRequested)
    {
        if (commandLine.apiKey.empty())
        {
            std::cerr << "Missing API key. Use --key=YOUR_XAI_API_KEY\n";
            return 1;
        }

        const std::string conversationsPath =
            resourcePathForRefreshRead("resources/conversations.json");
        const std::string scenesPath =
            resourcePathForRefreshRead("resources/scenes.json");
        const std::string itemsPath =
            resourcePathForRefreshRead("resources/items.json");
        return XaiTtsClient::refreshBundledVoices(
            commandLine.apiKey,
            ".",
            conversationsPath,
            scenesPath,
            itemsPath,
            "",
            commandLine.forceRefreshVoices,
            commandLine.refreshAllVoices ? "" : commandLine.refreshFilter);
    }

    std::srand((unsigned int)std::time(nullptr));

    if (!initializeWindow(gameConfig))
        return 1;

    audioManager.initialize(".", gameConfig.audio);

    if (!loadDatabases())
    {
        TraceLog(LOG_ERROR, "Failed to load game databases (scenes, conversations, or fonts)");
        std::cerr << "Failed to load game data. Check the log above for the scene or conversation that failed to parse.\n";
        shutdown();
        return 1;
    }

    uiBackdrop.load(gameConfig.ui, ".");

    std::string startSceneId;
    LocationStruct locationStruct;
    if (!sceneDatabase.loadStartScene(locationStruct, startSceneId))
    {
        TraceLog(LOG_ERROR, "Failed to load starting scene from resources/scenes.json");
        shutdown();
        return 1;
    }

    const Vector2 screenSize = {
        (float)gameConfig.display.width,
        (float)gameConfig.display.height
    };

    session.reset(new GameSession(
        locationStruct,
        screenSize,
        sceneDatabase,
        itemDatabase,
        itemCombinationDatabase,
        milestoneDatabase,
        audioManager,
        gameConfig,
        uiBackdrop,
        startSceneId,
        gameConfigPath));

    if (gameConfig.display.fullscreen)
        session->applyDisplayConfig();

    SetTargetFPS(60);

    while (!WindowShouldClose() && !session->shouldQuit())
    {
        session->update();
        BeginDrawing();
        session->draw();
        EndDrawing();
    }

    shutdown();
    return 0;
}

}