#ifndef ROOM_LOADER_H
#define ROOM_LOADER_H

#include <LocationStruct.h>
#include <string>

namespace testgame
{

struct RoomData
{
    std::string id;
    std::string imagePath;
    std::string description;
    std::string examineDetails;
    MovementStruct movement;
    ActionStruct actions;
    bool isStart;
};

bool loadStartLocation(
    const std::string& configPath,
    const std::string& assetRoot,
    LocationStruct& outLocation);

}

#endif /* ROOM_LOADER_H */