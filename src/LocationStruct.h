#ifndef LOCATION_STRUCT_H
#define LOCATION_STRUCT_H

#include <ActionStruct.h>
#include <MovementStruct.h>
#include <raylib.h>
#include <string>

namespace testgame
{

struct LocationStruct
{
    Texture2D locationImage;
    std::string locationDescription;
    std::string examineDetails;
    Font descriptionFont;
    Font boldFont;
    MovementStruct movementFilter;
    ActionStruct actionFilter;
};

}

#endif /* LOCATION_STRUCT_H */