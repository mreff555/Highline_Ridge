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
    bool ownsLocationImage = true;
    bool isUnderConstruction = false;
    std::string locationDescription;
    std::string examineDetails;
    std::string examineFlag;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    bool useRequiresExamine = true;
    std::string useExit;
    Font descriptionFont;
    Font boldFont;
    Font uiFont;
    MovementStruct movementFilter;
    ActionStruct actionFilter;
};

}

#endif /* LOCATION_STRUCT_H */