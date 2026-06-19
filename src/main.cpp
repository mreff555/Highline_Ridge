#include <Location.h>
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
    const Vector2 screenSize = {1500, 1117};
    InitWindow(screenSize.x, screenSize.y, "Highline Ridge");
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

    SceneDatabase sceneDatabase;
    const bool scenesLoaded =
        sceneDatabase.load("resources/scenes.json", ".") ||
        sceneDatabase.load("../resources/scenes.json", "..");
    if (!scenesLoaded)
    {
        TraceLog(LOG_ERROR, "Failed to load scenes from resources/scenes.json");
        CloseWindow();
        return 1;
    }

    std::string startSceneId;
    LocationStruct locationStruct;
    if (!sceneDatabase.loadStartScene(locationStruct, startSceneId))
    {
        TraceLog(LOG_ERROR, "Failed to load starting scene from resources/scenes.json");
        CloseWindow();
        return 1;
    }

    Location location(locationStruct, screenSize, sceneDatabase, startSceneId);

    SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
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
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}
