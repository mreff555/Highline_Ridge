#include <Location.h>
#include <raylib.h>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace testgame
{

const float Location::kScrollbarWidth = 16.0f;

namespace
{
    const Color kScrollTrack = {48, 44, 56, 255};
    const Color kScrollThumb = {140, 118, 72, 255};
    const Color kScrollThumbHover = {168, 142, 88, 255};
    const Color kChoiceText = {168, 138, 72, 255};
    const Color kChoiceHover = {210, 178, 108, 255};

    const char* kWakeOnFloorPrefix =
        "You come back to yourself on the floor of the cabin, cheek pressed to the Persian rug, "
        "the fireplace crackling somewhere near your shoulder. Your mouth tastes of ash and bourbon. "
        "How you got here from the saloon is a blank page. How you got here at all is no clearer than before.";
}

    Location::Location(const LocationStruct& locationStruct, Vector2 screenSize, RoomDatabase& roomDatabase, const std::string& roomId)
    : screenWidth((int)screenSize.x),
      screenHeight((int)screenSize.y),
      roomDatabase(roomDatabase),
      currentRoomId(roomId),
      locationImage(locationStruct.locationImage),
      baseDescription(locationStruct.locationDescription),
      narrativeText(locationStruct.locationDescription),
      examineDetails(locationStruct.examineDetails),
      speakDetails(locationStruct.speakDetails),
      useDetails(locationStruct.useDetails),
      useHealthDelta(locationStruct.useHealthDelta),
      useEnergyDelta(locationStruct.useEnergyDelta),
      useRepeatStatus(locationStruct.useRepeatStatus),
      baseActionFilter(locationStruct.actionFilter),
      descriptionFont(locationStruct.descriptionFont),
      boldFont(locationStruct.boldFont),
      up(locationStruct.movementFilter.up),
      down(locationStruct.movementFilter.down),
      forward(locationStruct.movementFilter.forward),
      backward(locationStruct.movementFilter.backward),
      left(locationStruct.movementFilter.left),
      right(locationStruct.movementFilter.right),
      textBox{screenWidth / 2.0f, 0, screenWidth / 2.0f, screenHeight * 2.0f / 3.0f},
      buttonBox{screenWidth / 2.0f, screenHeight * 2.0f / 3.0f, screenWidth / 2.0f, screenHeight / 3.0f},
      fullDialogHeight(screenHeight * 2.0f / 3.0f),
      buttonMgr(buttonBox, descriptionFont)
    {
        inventoryMgr.setFont(descriptionFont);
        const std::string& assetRoot = roomDatabase.getAssetRoot();
        const std::string fallbackRoot = (assetRoot == ".") ? ".." : ".";
        inventoryMgr.setAssetRoots(assetRoot, fallbackRoot);
        if (!inventoryMgr.ensureAssetsLoaded())
            TraceLog(LOG_WARNING, "Some inventory images failed to load at startup");
        trimNarrativeBuffer();
        conversationMgr.onEnterRoom(currentRoomId, roomDatabase.getSpeakConfig(currentRoomId));
        updateInventoryLayout();
        updateActionAvailability();
    }

    Location::~Location()
    {
        UnloadTexture(locationImage);
    }

    Texture2D Location::getImage() const
    {
        return locationImage;
    }
    char* Location::getDescription() const
    {
        return (char*)narrativeText.c_str();
    }
    const Font Location::getDescriptionFont() const
    {
        return descriptionFont;
    }
    bool Location::isUp() const
    {
        return up;
    }
    bool Location::isDown() const
    {
        return down;
    }
    bool Location::isForward() const
    {
        return forward;
    }
    bool Location::isBackward() const
    {
        return backward;
    }
    bool Location::isLeft() const
    {
        return left;
    }
    bool Location::isRight() const
    {
        return right;
    }

    float Location::getNarrativeLineHeight() const
    {
        const float scaleFactor = fontSize / (float)descriptionFont.baseSize;
        return (descriptionFont.baseSize + descriptionFont.baseSize / 2.0f) * scaleFactor;
    }

    Rectangle Location::getMainImageBounds() const
    {
        return { 0.0f, 0.0f, (float)screenWidth * 0.5f, (float)screenHeight };
    }

    void Location::drawMainImage() const
    {
        const Rectangle mainBounds = getMainImageBounds();

        if (inventoryMgr.isOpen() && inventoryMgr.isExaminingItem())
        {
            const InventoryItem* item = inventoryMgr.getSelectedItem();
            if (item != nullptr && item->examineImage.id != 0)
            {
                DrawTexturePro(
                    item->examineImage,
                    { 0.0f, 0.0f, (float)item->examineImage.width, (float)item->examineImage.height },
                    mainBounds,
                    { 0.0f, 0.0f },
                    0.0f,
                    WHITE);
                return;
            }
        }

        DrawTexturePro(
            locationImage,
            { 0.0f, 0.0f, (float)locationImage.width, (float)locationImage.height },
            mainBounds,
            { 0.0f, 0.0f },
            0.0f,
            WHITE);
    }

PLACEHOLDER_REMAINING