#include <Location.h>
#include <RoomLoader.h>
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
        if (FileExists("resources/rooms.json"))
            return true;

        if (FileExists("../resources/rooms.json"))
        {
            ChangeDirectory("..");
            return true;
        }

        const char* appDir = GetApplicationDirectory();
        if (appDir != nullptr && appDir[0] != '\0')
        {
            const std::string resourcePath = std::string(appDir) + "/resources/rooms.json";
            if (FileExists(resourcePath.c_str()))
            {
                ChangeDirectory(appDir);
                return true;
            }

            const std::string parentResourcePath = std::string(appDir) + "/../resources/rooms.json";
            if (FileExists(parentResourcePath.c_str()))
            {
                ChangeDirectory((std::string(appDir) + "/..").c_str());
                return true;
            }
        }

        return false;
    };

    if (!locateResources())
        TraceLog(LOG_WARNING, "Could not locate resources/rooms.json from current working directory");

    RoomDatabase roomDatabase;
    const bool roomsLoaded =
        roomDatabase.load("resources/rooms.json", ".") ||
        roomDatabase.load("../resources/rooms.json", "..");
    if (!roomsLoaded)
    {
        TraceLog(LOG_ERROR, "Failed to load rooms from resources/rooms.json");
        CloseWindow();
        return 1;
    }

    std::string startRoomId;
    LocationStruct locationStruct;
    if (!roomDatabase.loadStartRoom(locationStruct, startRoomId))
    {
        TraceLog(LOG_ERROR, "Failed to load starting room from resources/rooms.json");
        CloseWindow();
        return 1;
    }

    Location location(locationStruct, screenSize, roomDatabase, startRoomId);

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
