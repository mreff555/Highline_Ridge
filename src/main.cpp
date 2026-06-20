#include <AudioManager.h>
#include <GameConfig.h>
#include <Location.h>
#include <MilestoneDatabase.h>
#include <SceneLoader.h>
#include <raylib.h>
#include <cstdlib>
#include <ctime>
#include <stdio.h>
#include <string>

using namespace testgame;

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    std::srand((unsigned int)std::time(nullptr));

    auto locateResources = []() -> bool
    {
        if (FileExists("resources/scenes.json"))
            return true;

        if (FileExists("../resources/scenes.json"))
        {
            ChangeDirectory("..");
            return true;
        }

        const char* appDir = GetApplicationDirectory();
        if (appDir != nullptr && appDir[0] != '\0')
        {
            const std::string resourcePath = std::string(appDir) + "/resources/scenes.json";
            if (FileExists(resourcePath.c_str()))
            {
                ChangeDirectory(appDir);
                return true;
            }

            const std::string parentResourcePath = std::string(appDir) + "/../resources/scenes.json";
            if (FileExists(parentResourcePath.c_str()))
            {
                ChangeDirectory((std::string(appDir) + "/..").c_str());
                return true;
            }
        }

        return false;
    };

    if (!locateResources())
        TraceLog(LOG_WARNING, "Could not locate resources/scenes.json from current working directory");

    std::string gameConfigPath = "resources/game_config.json";
    GameConfig gameConfig;
    if (!loadGameConfig(gameConfigPath, gameConfig))
    {
        if (loadGameConfig("../resources/game_config.json", gameConfig))
            gameConfigPath = "../resources/game_config.json";
        else
            TraceLog(LOG_WARNING, "Failed to load resources/game_config.json; using defaults");
    }

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