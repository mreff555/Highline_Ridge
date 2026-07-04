#include "GameApplication.h"

#include <GameConfig.h>
#include <LocationStruct.h>
#include <PlatformPath.h>
#include <XaiTtsClient.h>
#include <raylib.h>
#include <cstdlib>
#include <ctime>
#include <iostream>

namespace highline_ridge
{

namespace
{

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
    const bool scenesLoaded =
        sceneDatabase.load("resources/scenes.json", ".") ||
        sceneDatabase.load("../resources/scenes.json", "..");
    if (!scenesLoaded)
    {
        TraceLog(LOG_ERROR, "Failed to load scenes from resources/scenes.json");
        return false;
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

    const bool combinationsLoaded =
        itemCombinationDatabase.load("resources/item_combinations.json") ||
        itemCombinationDatabase.load("../resources/item_combinations.json");
    if (!combinationsLoaded)
        TraceLog(LOG_WARNING, "Failed to load item combinations from resources/item_combinations.json");

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
    const CommandLineOptions commandLine = parseCommandLine(argc, argv);
    if (commandLine.showHelp)
    {
        printGameHelp(argc > 0 ? argv[0] : nullptr);
        return 0;
    }

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

    if (!loadGameConfig(gameConfigPath, gameConfig))
    {
        if (loadGameConfig("../resources/game_config.json", gameConfig))
            gameConfigPath = "../resources/game_config.json";
        else
            TraceLog(LOG_WARNING, "Failed to load resources/game_config.json; using defaults");
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
        return XaiTtsClient::refreshBundledVoices(
            commandLine.apiKey,
            ".",
            conversationsPath,
            scenesPath,
            gameConfig.tts.voiceId,
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