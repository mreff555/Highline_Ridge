#include "RoomLoader.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <raylib.h>
namespace testgame
{

namespace
{

bool parseMovement(const nlohmann::json& movement, MovementStruct& out)
{
    if (!movement.is_object())
        return false;

    out.forward = movement.value("forward", false);
    out.backward = movement.value("backward", false);
    out.left = movement.value("left", false);
    out.right = movement.value("right", false);
    return true;
}

bool parseActions(const nlohmann::json& actions, ActionStruct& out)
{
    if (!actions.is_object())
        return false;

    out.examine = actions.value("examine", false);
    out.speak = actions.value("speak", false);
    out.hit = actions.value("hit", false);
    out.use = actions.value("use", false);
    return true;
}

bool parseRoom(const std::string& id, const nlohmann::json& room, RoomData& out)
{
    if (!room.is_object())
        return false;

    out.id = id;
    out.isStart = room.value("start", false);
    out.imagePath = room.value("image", "");
    out.description = room.value("description", "");
    out.examineDetails = room.value("examineDetails", "");

    if (out.imagePath.empty() || out.description.empty())
        return false;

    if (!parseMovement(room.value("movement", nlohmann::json::object()), out.movement))
        return false;

    if (!parseActions(room.value("actions", nlohmann::json::object()), out.actions))
        return false;

    return true;
}

std::string resolveAssetPath(const std::string& assetRoot, const std::string& path)
{
    if (path.empty())
        return path;

    if (path[0] == '/')
        return path;

    if (assetRoot.empty())
        return path;

    if (assetRoot.back() == '/')
        return assetRoot + path;

    return assetRoot + "/" + path;
}

}

bool loadStartLocation(
    const std::string& configPath,
    const std::string& assetRoot,
    LocationStruct& outLocation)
{
    std::ifstream file(configPath.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    if (!config.is_object() || !config.contains("rooms") || !config["rooms"].is_object())
        return false;

    const std::string fontPath = config.value("font", "");
    const std::string boldFontPath = config.value("boldFont", fontPath);
    if (fontPath.empty())
        return false;

    const nlohmann::json& rooms = config["rooms"];
    RoomData startRoom;
    bool foundStart = false;

    for (auto it = rooms.begin(); it != rooms.end(); ++it)
    {
        RoomData room;
        if (!parseRoom(it.key(), it.value(), room))
            continue;

        if (room.isStart)
        {
            if (foundStart)
                return false;

            startRoom = room;
            foundStart = true;
        }
    }

    if (!foundStart)
        return false;

    const std::string imagePath = resolveAssetPath(assetRoot, startRoom.imagePath);
    const Texture2D texture = LoadTexture(imagePath.c_str());
    if (texture.id == 0)
        return false;

    const Font font = LoadFont(fontPath.c_str());
    if (font.texture.id == 0)
    {
        UnloadTexture(texture);
        return false;
    }

    Font boldFont = LoadFont(boldFontPath.c_str());
    if (boldFont.texture.id == 0)
        boldFont = font;

    outLocation.locationImage = texture;
    outLocation.locationDescription = startRoom.description;
    outLocation.examineDetails = startRoom.examineDetails;
    outLocation.descriptionFont = font;
    outLocation.boldFont = boldFont;
    outLocation.movementFilter = startRoom.movement;
    outLocation.actionFilter = startRoom.actions;

    return true;
}

}