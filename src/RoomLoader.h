#ifndef ROOM_LOADER_H
#define ROOM_LOADER_H

#include <ConversationStruct.h>
#include <LocationStruct.h>
#include <map>
#include <string>

namespace testgame
{

std::string resolveAssetPath(const std::string& assetRoot, const std::string& path);

struct RoomData
{
    std::string id;
    std::string imagePath;
    std::string description;
    std::string examineDetails;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    MovementStruct movement;
    ActionStruct actions;
    bool isStart;
    std::map<std::string, std::string> exits;
    RoomSpeakConfig speakConfig;
};

class RoomDatabase
{
    public:
    RoomDatabase();
    ~RoomDatabase();

    bool load(const std::string& configPath, const std::string& assetRoot);
    bool loadStartRoom(LocationStruct& outLocation, std::string& outRoomId) const;
    bool loadRoom(const std::string& roomId, LocationStruct& outLocation) const;
    std::string getExitRoomId(const std::string& roomId, const std::string& direction) const;
    const RoomSpeakConfig& getSpeakConfig(const std::string& roomId) const;
    const std::string& getAssetRoot() const { return assetRoot; }

    private:
    bool buildLocationStruct(const RoomData& room, LocationStruct& outLocation) const;

    std::map<std::string, RoomData> rooms;
    Font descriptionFont;
    Font boldFont;
    bool fontsLoaded;
    std::string assetRoot;
};

bool loadStartLocation(
    const std::string& configPath,
    const std::string& assetRoot,
    LocationStruct& outLocation);

}

#endif /* ROOM_LOADER_H */
