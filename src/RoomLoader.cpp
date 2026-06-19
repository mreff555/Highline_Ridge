#include "RoomLoader.h"
#include "ImageCompression.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <raylib.h>
#include <vector>

namespace testgame
{

namespace
{

Font loadGameFont(const std::string& assetRoot, const std::string& fontPath)
{
    std::vector<std::string> paths;
    auto tryAdd = [&](const std::string& candidate)
    {
        if (candidate.empty())
            return;

        if (std::find(paths.begin(), paths.end(), candidate) == paths.end())
            paths.push_back(candidate);
    };

    tryAdd(fontPath);
    tryAdd(resolveAssetPath(assetRoot, fontPath));
    tryAdd(resolveAssetPath(".", fontPath));
    tryAdd(resolveAssetPath("..", fontPath));

    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0')
    {
        const std::string appDirectory(appDir);
        tryAdd(resolveAssetPath(appDirectory, fontPath));
        tryAdd(resolveAssetPath(appDirectory + "/..", fontPath));
    }

    for (const std::string& path : paths)
    {
        if (!FileExists(path.c_str()))
            continue;

        Font font = LoadFontEx(path.c_str(), 64, nullptr, 0);
        if (font.texture.id != 0)
        {
            TraceLog(LOG_INFO, "Loaded game font: %s", path.c_str());
            return font;
        }
    }

    return Font{};
}

bool parseMovement(const nlohmann::json& movement, MovementStruct& out)
{
    if (!movement.is_object())
        return false;

    out.up = movement.value("up", false);
    out.down = movement.value("down", false);
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

bool parseStatusEffect(const nlohmann::json& status, StatusEffect& out)
{
    if (!status.is_object())
        return true;

    out.key = status.value("key", "");
    out.health = status.value("health", 0.0f);
    out.energy = status.value("energy", 0.0f);
    out.tenacity = status.value("tenacity", 0.0f);
    out.lucidity = status.value("lucidity", 0.0f);
    out.repeat = status.value("repeat", false);
    out.onZeroLucidity = status.value("onZeroLucidity", "");
    return true;
}

ConversationPhaseType parsePhaseType(const std::string& type)
{
    if (type == "scripted")
        return ConversationPhaseType::Scripted;
    if (type == "random")
        return ConversationPhaseType::Random;
    return ConversationPhaseType::Once;
}

bool parseConversationChoice(const nlohmann::json& choice, ConversationChoiceDef& out)
{
    if (!choice.is_object())
        return false;

    out.id = choice.value("id", "");
    out.label = choice.value("label", "");
    out.response = choice.value("response", "");
    if (!parseStatusEffect(choice.value("status", nlohmann::json::object()), out.status))
        return false;

    return !out.id.empty() && !out.label.empty() && !out.response.empty();
}

bool parseRandomLine(const nlohmann::json& line, RandomConversationLine& out)
{
    if (!line.is_object())
        return false;

    out.id = line.value("id", "");
    out.text = line.value("text", "");
    out.weight = line.value("weight", 1);
    if (!parseStatusEffect(line.value("status", nlohmann::json::object()), out.status))
        return false;

    return !out.text.empty();
}

bool parseConversationPhase(const nlohmann::json& phase, ConversationPhase& out)
{
    if (!phase.is_object())
        return false;

    out.id = phase.value("id", "");
    out.type = parsePhaseType(phase.value("type", "once"));
    out.requiresPhaseId = phase.value("requiresPhase", "");
    out.requiresFlag = phase.value("requiresFlag", "");
    out.resetOnRoomEnter = phase.value("resetOnRoomEnter", true);
    out.text = phase.value("text", "");
    out.intro = phase.value("intro", "");
    out.poolId = phase.value("poolId", "");
    out.avoidRepeat = phase.value("avoidRepeat", true);

    if (!parseStatusEffect(phase.value("status", nlohmann::json::object()), out.status))
        return false;

    out.choices.clear();
    const nlohmann::json& choices = phase.value("choices", nlohmann::json::array());
    if (!choices.is_array())
        return false;

    for (const nlohmann::json& choice : choices)
    {
        ConversationChoiceDef parsed;
        if (!parseConversationChoice(choice, parsed))
            return false;
        out.choices.push_back(parsed);
    }

    out.lines.clear();
    const nlohmann::json& lines = phase.value("lines", nlohmann::json::array());
    if (!lines.is_array())
        return false;

    for (const nlohmann::json& line : lines)
    {
        RandomConversationLine parsed;
        if (!parseRandomLine(line, parsed))
            return false;
        out.lines.push_back(parsed);
    }

    if (out.id.empty())
        return false;

    if (out.type == ConversationPhaseType::Once && out.text.empty())
        return false;

    if (out.type == ConversationPhaseType::Scripted &&
        (out.intro.empty() || out.choices.empty()))
        return false;

    if (out.type == ConversationPhaseType::Random && out.lines.empty())
        return false;

    return true;
}

bool parseSpeakPhasesArray(const nlohmann::json& phases, RoomSpeakConfig& out)
{
    if (!phases.is_array())
        return false;

    out.phases.clear();
    for (const nlohmann::json& phase : phases)
    {
        ConversationPhase parsed;
        if (!parseConversationPhase(phase, parsed))
            return false;
        out.phases.push_back(parsed);
    }

    return true;
}

bool parseSpeakConfig(const nlohmann::json& room, RoomSpeakConfig& out)
{
    out.phases.clear();

    if (room.contains("conversations") &&
        room["conversations"].is_object() &&
        room["conversations"].contains("speakPhases"))
    {
        return parseSpeakPhasesArray(room["conversations"]["speakPhases"], out);
    }

    if (room.contains("speakPhases"))
        return parseSpeakPhasesArray(room["speakPhases"], out);

    const std::string speakDetails = room.value("speakDetails", "");
    if (speakDetails.empty())
        return true;

    ConversationPhase legacy;
    legacy.id = "speak";
    legacy.type = ConversationPhaseType::Once;
    legacy.text = speakDetails;
    legacy.resetOnRoomEnter = true;
    if (!parseStatusEffect(room.value("speakStatus", nlohmann::json::object()), legacy.status))
        return false;
    out.phases.push_back(legacy);
    return true;
}

bool applyConversationOverlays(
    const nlohmann::json& overlays,
    std::map<std::string, RoomData>& rooms)
{
    if (!overlays.is_object())
        return true;

    for (auto it = overlays.begin(); it != overlays.end(); ++it)
    {
        std::map<std::string, RoomData>::iterator roomIt = rooms.find(it.key());
        if (roomIt == rooms.end())
            return false;

        if (!parseSpeakConfig(it.value(), roomIt->second.speakConfig))
            return false;
    }

    return true;
}

std::string siblingConfigPath(const std::string& configPath, const std::string& filename)
{
    const size_t slash = configPath.find_last_of('/');
    if (slash == std::string::npos)
        return filename;
    return configPath.substr(0, slash + 1) + filename;
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
    out.examineFlag = room.value("examineFlag", "");
    out.speakDetails = room.value("speakDetails", "");
    out.useDetails = room.value("useDetails", "");
    out.useHealthDelta = room.value("useHealthDelta", 0.0f);
    out.useEnergyDelta = room.value("useEnergyDelta", 0.0f);
    out.useRepeatStatus = room.value("useRepeatStatus", false);

    if (out.description.empty())
        return false;

    if (!parseMovement(room.value("movement", nlohmann::json::object()), out.movement))
        return false;

    if (!parseActions(room.value("actions", nlohmann::json::object()), out.actions))
        return false;

    if (!parseExits(room.value("exits", nlohmann::json::object()), out.exits))
        return false;

    if (!parseSpeakConfig(room, out.speakConfig))
        return false;

    return true;
}

}

bool loadResourceTexture(
    const std::string& assetRoot,
    const std::string& relativePath,
    Texture2D& outTexture)
{
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relativePath);

    for (const std::string& path : paths)
    {
        if (FileExists(path.c_str()))
        {
            Texture2D texture = LoadTexture(path.c_str());
            if (texture.id != 0)
            {
                outTexture = texture;
                TraceLog(LOG_INFO, "Loaded resource texture: %s", path.c_str());
                return true;
            }

            if (loadTextureFromAssetFile(path, outTexture))
            {
                TraceLog(LOG_INFO, "Loaded resource texture: %s", path.c_str());
                return true;
            }
        }

        const std::string compressedPath = compressedAssetPath(path);
        if (FileExists(compressedPath.c_str()) &&
            loadTextureFromAssetFile(compressedPath, outTexture))
        {
            TraceLog(LOG_INFO, "Loaded compressed resource texture: %s", compressedPath.c_str());
            return true;
        }
    }

    TraceLog(LOG_ERROR, "Failed to load resource texture: %s", relativePath.c_str());
    return false;
}

RoomDatabase::RoomDatabase()
    : descriptionFont{},
      boldFont{},
      uiFont{},
      fontsLoaded(false)
{
}

RoomDatabase::~RoomDatabase()
{
    if (underConstructionImageReady && underConstructionImage.data != nullptr)
        UnloadImage(underConstructionImage);

    if (fontsLoaded)
    {
        UnloadFont(descriptionFont);
        if (boldFont.texture.id != descriptionFont.texture.id)
            UnloadFont(boldFont);
        if (uiFont.texture.id != descriptionFont.texture.id &&
            uiFont.texture.id != boldFont.texture.id)
        {
            UnloadFont(uiFont);
        }
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
    const std::string uiFontPath = config.value("uiFont", "");
    if (fontPath.empty())
        return false;

    if (!fontsLoaded)
    {
        descriptionFont = loadGameFont(assetRoot, fontPath);
        if (descriptionFont.texture.id == 0)
            return false;

        boldFont = loadGameFont(assetRoot, boldFontPath);
        if (boldFont.texture.id == 0)
            boldFont = descriptionFont;

        if (!uiFontPath.empty())
            uiFont = loadGameFont(assetRoot, uiFontPath);

        if (uiFont.texture.id == 0)
        {
            uiFont = loadGameFont(assetRoot, "/System/Library/Fonts/Supplemental/Courier New.ttf");
            if (uiFont.texture.id == 0)
                uiFont = descriptionFont;
        }

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

    if (config.contains("conversations"))
    {
        if (!applyConversationOverlays(config["conversations"], rooms))
            return false;
    }
    else
    {
        const std::string conversationsPath = siblingConfigPath(configPath, "conversations.json");
        std::ifstream conversationsFile(conversationsPath.c_str());
        if (conversationsFile.is_open())
        {
            nlohmann::json conversationsConfig;
            try
            {
                conversationsFile >> conversationsConfig;
            }
            catch (const nlohmann::json::exception&)
            {
                return false;
            }

            if (!applyConversationOverlays(conversationsConfig, rooms))
                return false;
        }
    }

    return !rooms.empty();
}

bool RoomDatabase::tryLoadRoomImage(const std::string& imagePath, Texture2D& outTexture) const
{
    if (imagePath.empty())
        return false;

    Texture2D texture{};
    if (loadResourceTexture(assetRoot, imagePath, texture))
    {
        outTexture = texture;
        return true;
    }

    return false;
}

void RoomDatabase::ensureUnderConstructionImage() const
{
    if (underConstructionImageReady && underConstructionImage.data != nullptr)
        return;

    Texture2D assetTexture{};
    if (loadResourceTexture(assetRoot, "resources/room_under_construction.png", assetTexture))
    {
        underConstructionImage = LoadImageFromTexture(assetTexture);
        UnloadTexture(assetTexture);
        if (underConstructionImage.data != nullptr)
        {
            underConstructionImageReady = true;
            TraceLog(LOG_INFO, "Loaded under-construction image from resources/room_under_construction.png");
            return;
        }
    }

    const int width = 1024;
    const int height = 768;
    RenderTexture2D renderTarget = LoadRenderTexture(width, height);

    BeginTextureMode(renderTarget);
    ClearBackground((Color){34, 32, 40, 255});

    const Color accent = {186, 150, 72, 255};
    const Color stripe = {168, 132, 48, 255};
    DrawRectangle(0, 0, width, 10, accent);
    DrawRectangle(0, height - 10, width, 10, accent);

    for (int x = -height; x < width; x += 48)
        DrawRectangle(x, height / 2 - 36, 24, 72, stripe);

    const char* title = "UNDER CONSTRUCTION";
    const char* subtitle = "This location is not yet complete.";
    const float titleSize = 52.0f;
    const float subtitleSize = 24.0f;
    const Vector2 titleMeasure = MeasureTextEx(descriptionFont, title, titleSize, 2.0f);
    const Vector2 subtitleMeasure = MeasureTextEx(descriptionFont, subtitle, subtitleSize, 1.0f);

    DrawTextEx(
        descriptionFont,
        title,
        { (width - titleMeasure.x) * 0.5f, height * 0.36f },
        titleSize,
        2.0f,
        accent);

    DrawTextEx(
        descriptionFont,
        subtitle,
        { (width - subtitleMeasure.x) * 0.5f, height * 0.36f + titleMeasure.y + 18.0f },
        subtitleSize,
        1.0f,
        (Color){176, 168, 152, 255});

    EndTextureMode();

    Image generated = LoadImageFromTexture(renderTarget.texture);
    ImageFlipVertical(&generated);
    UnloadRenderTexture(renderTarget);

    underConstructionImage = generated;
    underConstructionImageReady = true;
    TraceLog(LOG_INFO, "Generated under-construction placeholder image");
}

Texture2D RoomDatabase::createOwnedPlaceholderTexture() const
{
    ensureUnderConstructionImage();

    Texture2D texture = LoadTextureFromImage(underConstructionImage);
    if (texture.id != 0)
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);

    return texture;
}

bool RoomDatabase::buildLocationStruct(const RoomData& room, LocationStruct& outLocation) const
{
    outLocation.locationDescription = room.description;
    outLocation.examineDetails = room.examineDetails;
    outLocation.examineFlag = room.examineFlag;
    outLocation.speakDetails = room.speakDetails;
    outLocation.useDetails = room.useDetails;
    outLocation.useHealthDelta = room.useHealthDelta;
    outLocation.useEnergyDelta = room.useEnergyDelta;
    outLocation.useRepeatStatus = room.useRepeatStatus;
    outLocation.descriptionFont = descriptionFont;
    outLocation.boldFont = boldFont;
    outLocation.uiFont = uiFont;
    outLocation.movementFilter = room.movement;
    outLocation.actionFilter = room.actions;
    outLocation.ownsLocationImage = true;
    outLocation.isUnderConstruction = false;

    Texture2D roomTexture{};
    if (tryLoadRoomImage(room.imagePath, roomTexture))
    {
        outLocation.locationImage = roomTexture;
        return true;
    }

    if (!room.imagePath.empty())
        TraceLog(LOG_WARNING, "Room '%s' image unavailable (%s); using under-construction placeholder", room.id.c_str(), room.imagePath.c_str());
    else
        TraceLog(LOG_INFO, "Room '%s' has no image; using under-construction placeholder", room.id.c_str());

    outLocation.isUnderConstruction = true;
    outLocation.locationImage = createOwnedPlaceholderTexture();
    outLocation.ownsLocationImage = (outLocation.locationImage.id != 0);
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

const RoomSpeakConfig& RoomDatabase::getSpeakConfig(const std::string& roomId) const
{
    static const RoomSpeakConfig kEmptyConfig;
    std::map<std::string, RoomData>::const_iterator it = rooms.find(roomId);
    if (it == rooms.end())
        return kEmptyConfig;

    return it->second.speakConfig;
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
