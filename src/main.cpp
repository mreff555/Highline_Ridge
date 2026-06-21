#include <AudioManager.h>
#include <GameConfig.h>
#include <ItemDatabase.h>
#include <Location.h>
#include <MilestoneDatabase.h>
#include <SceneLoader.h>
#include <XaiTtsClient.h>
#include <raylib.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdio.h>
#include <string>

using namespace testgame;

namespace
{

struct CommandLineOptions
{
    bool showHelp = false;
    std::string refreshVoicesApiKey;
};

bool locateGameResources()
{
    auto tryDirectory = [](const std::string& directory) -> bool
    {
        if (directory.empty())
            return false;

        const std::string scenesPath = directory + "/resources/scenes.json";
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

        const std::string parentDirectory = executableDirectory + "/..";
        if (tryDirectory(parentDirectory))
            return true;
    }

    if (tryDirectory("."))
        return true;

    if (tryDirectory(".."))
        return true;

    return false;
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

        const std::string refreshPrefix = "--refresh-voices=";
        if (argument.compare(0, refreshPrefix.size(), refreshPrefix) == 0)
        {
            options.refreshVoicesApiKey = argument.substr(refreshPrefix.size());
            continue;
        }

        std::cerr << "Unknown option: " << argument << "\n";
        options.showHelp = true;
    }

    return options;
}

}

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    const CommandLineOptions commandLine = parseCommandLine(argc, argv);
    if (commandLine.showHelp)
    {
        printGameHelp(argc > 0 ? argv[0] : nullptr);
        return 0;
    }

    if (!locateGameResources())
    {
        TraceLog(LOG_WARNING, "Could not locate resources/scenes.json next to executable or working directory");
        if (!commandLine.refreshVoicesApiKey.empty())
        {
            std::cerr << "Cannot refresh voices without game resources.\n";
            return 1;
        }
    }

    std::string gameConfigPath = "resources/game_config.json";
    GameConfig gameConfig;
    if (!loadGameConfig(gameConfigPath, gameConfig))
    {
        if (loadGameConfig("../resources/game_config.json", gameConfig))
            gameConfigPath = "../resources/game_config.json";
        else
            TraceLog(LOG_WARNING, "Failed to load resources/game_config.json; using defaults");
    }

    if (!commandLine.refreshVoicesApiKey.empty())
    {
        const std::string conversationsPath = "resources/conversations.json";
        return XaiTtsClient::refreshBundledVoices(
            commandLine.refreshVoicesApiKey,
            ".",
            conversationsPath,
            gameConfig.tts.voiceId);
    }

    // Initialization
    //--------------------------------------------------------------------------------------
    std::srand((unsigned int)std::time(nullptr));

    const Vector2 screenSize = {
        (float)gameConfig.display.width,
        (float)gameConfig.display.height
    };

    InitWindow(screenSize.x, screenSize.y, "Highline Ridge");
    SetExitKey(0);
    if (gameConfig.display.fullscreen)
        ToggleFullscreen();
    if (!IsAudioDeviceReady())
        InitAudioDevice();

    AudioManager audioManager;
    audioManager.initialize(".", gameConfig.audio);

    SceneDatabase sceneDatabase;
    const bool scenesLoaded =
        sceneDatabase.load("resources/scenes.json", ".") ||
        sceneDatabase.load("../resources/scenes.json", "..");
    if (!scenesLoaded)
    {
        TraceLog(LOG_ERROR, "Failed to load scenes from resources/scenes.json");
        audioManager.shutdown();
        CloseWindow();
        return 1;
    }

    MilestoneDatabase milestoneDatabase;
    const bool milestonesLoaded =
        milestoneDatabase.load("resources/milestones.json") ||
        milestoneDatabase.load("../resources/milestones.json");
    if (!milestonesLoaded)
        TraceLog(LOG_WARNING, "Failed to load milestones from resources/milestones.json");

    ItemDatabase itemDatabase;
    const bool itemsLoaded =
        itemDatabase.load("resources/items.json", ".") ||
        itemDatabase.load("../resources/items.json", "..");
    if (!itemsLoaded)
        TraceLog(LOG_WARNING, "Failed to load items from resources/items.json");

    std::string startSceneId;
    LocationStruct locationStruct;
    if (!sceneDatabase.loadStartScene(locationStruct, startSceneId))
    {
        TraceLog(LOG_ERROR, "Failed to load starting scene from resources/scenes.json");
        audioManager.shutdown();
        CloseWindow();
        return 1;
    }

    Location location(
        locationStruct,
        screenSize,
        sceneDatabase,
        itemDatabase,
        milestoneDatabase,
        audioManager,
        gameConfig,
        startSceneId,
        gameConfigPath);

    if (gameConfig.display.fullscreen)
        location.applyDisplayConfig();

    SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose() && !location.shouldQuit())
    {
        // Update
        //----------------------------------------------------------------------------------
        location.update();
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            location.draw();

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    audioManager.shutdown();
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}