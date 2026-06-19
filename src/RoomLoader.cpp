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
    out.take = actions.value("take", false);
    return true;
}

bool parseExits(const nlohmann::json& exits, std::map<std::string, std::string>& out)
{
    out.clear();
    if (!exits.is_object())
        return true;

    for (auto it = exits.begin(); it != exits.end(); ++it)
    {
        if (it.value().is_string())
            out[it.key()] = it.value().get<std::string>();
    }

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
    out.speakDetails = room.value("speakDetails", "");
    out.useDetails = room.value("useDetails", "");
    out.useHealthDelta = room.value("useHealthDelta", 0.0f);
    out.useEnergyDelta = room.value("useEnergyDelta", 0.0f);
    out.useRepeatStatus = room.value("useRepeatStatus", false);

    if (out.imagePath.empty() || out.description.empty())
        return false;

    if (!parseMovement(room.value("movement", nlohmann::json::object()), out.movement))
        return false;

    if (!parseActions(room.value("actions", nlohmann::json::object()), out.actions))
        return false;

    if (!parseExits(room.value("exits", nlohmann::json::object()), out.exits))
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

RoomDatabase::RoomDatabase()
    : descriptionFont{},
      boldFont{},
      fontsLoaded(false)
{
}

RoomDatabase::~RoomDatabase()
{
    if (fontsLoaded)
    {
        UnloadFont(descriptionFont);
        if (boldFont.texture.id != descriptionFont.texture.id)
            UnloadFont(boldFont);
    }
}

bool RoomDatabase::load(const std::string& configPath, const std::string& assetRoot)
{
    this->assetRoot = assetRoot;
    rooms.clear();

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

    if (!fontsLoaded)
    {
        descriptionFont = LoadFont(fontPath.c_str());
        if (descriptionFont.texture.id == 0)
            return false;

        boldFont = LoadFont(boldFontPath.c_str());
        if (boldFont.texture.id == 0)
            boldFont = descriptionFont;

        fontsLoaded = true;
    }

    const nlohmann::json& roomsJson = config["rooms"];
    for (auto it = roomsJson.begin(); it != roomsJson.end(); ++it)
    {
        RoomData room;
        if (!parseRoom(it.key(), it.value(), room))
            return false;

        rooms[room.id] = room;
    }

    return !rooms.empty();
}

bool RoomDatabase::buildLocationStruct(const RoomData& room, LocationStruct& outLocation) const
{
    const std::string primaryPath = resolveAssetPath(assetRoot, room.imagePath);
    std::string imagePath = primaryPath;

    if (!FileExists(imagePath.c_str()) && FileExists(room.imagePath.c_str()))
        imagePath = room.imagePath;

    if (!FileExists(imagePath.c_str()))
    {
        TraceLog(LOG_ERROR, "Room image not found: %s", primaryPath.c_str());
        return false;
    }

    const Texture2D texture = LoadTexture(imagePath.c_str());
    if (texture.id == 0)
    {
        TraceLog(LOG_ERROR, "Failed to load room image: %s", imagePath.c_str());
        return false;
    }

    outLocation.locationImage = texture;
    outLocation.locationDescription = room.description;
    outLocation.examineDetails = room.examineDetails;
    outLocation.speakDetails = room.speakDetails;
    outLocation.useDetails = room.useDetails;
    outLocation.useHealthDelta = room.useHealthDelta;
    outLocation.useEnergyDelta = room.useEnergyDelta;
    outLocation.useRepeatStatus = room.useRepeatStatus;
    outLocation.descriptionFont = descriptionFont;
    outLocation.boldFont = boldFont;
    outLocation.movementFilter = room.movement;
    outLocation.actionFilter = room.actions;

    return true;
}

bool RoomDatabase::loadStartRoom(LocationStruct& outLocation, std::string& outRoomId) const
{
    for (std::map<std::string, RoomData>::const_iterator it = rooms.begin(); it != rooms.end(); ++it)
    {
        if (it->second.isStart)
        {
            outRoomId = it->first;
            return loadRoom(outRoomId, outLocation);
        }
    }

    return false;
}

bool RoomDatabase::loadRoom(const std::string& roomId, LocationStruct& outLocation) const
{
    std::map<std::string, RoomData>::const_iterator it = rooms.find(roomId);
    if (it == rooms.end())
        return false;

    return buildLocationStruct(it->second, outLocation);
}

std::string RoomDatabase::getExitRoomId(const std::string& roomId, const std::string& direction) const
{
    std::map<std::string, RoomData>::const_iterator it = rooms.find(roomId);
    if (it == rooms.end())
        return "";

    std::map<std::string, std::string>::const_iterator exitIt = it->second.exits.find(direction);
    if (exitIt == it->second.exits.end())
        return "";

    return exitIt->second;
}

bool loadStartLocation(
    const std::string& configPath,
    const std::string& assetRoot,
    LocationStruct& outLocation)
{
    RoomDatabase database;
    std::string roomId;
    if (!database.load(configPath, assetRoot))
        return false;

    return database.loadStartRoom(outLocation, roomId);
}

}