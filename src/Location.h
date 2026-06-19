#ifndef LOCATION_H
#define LOCATION_H

#include <LocationStruct.h>
#include <ButtonMgr.h>
#include <RoomLoader.h>
#include <raylib.h>
#include <string>
namespace testgame
{

class Location
{
    public:
    Location(const LocationStruct& locationStruct, Vector2 screenSize, RoomDatabase& roomDatabase, const std::string& roomId);

    virtual ~Location();

    Texture2D getImage() const;
     char* getDescription() const;
    const Font getDescriptionFont() const;
    bool isForward() const;
    bool isBackward() const;
    bool isLeft() const;
    bool isRight() const;

    void update();
    void draw() const;
    void appendExamineDetails();

    private:
    void applyLocationStruct(const LocationStruct& locationStruct);
    void tryMove(const std::string& direction);
    void trimNarrativeBuffer();
    void rebuildNarrativeLayout() const;
    void handleNarrativeScrollInput();
    void drawNarrativeText() const;
    void drawNarrativeScrollbar() const;
    float getNarrativeVisibleHeight() const;
    float getNarrativeLineHeight() const;
    float getNarrativeWrapWidth() const;
    void layoutWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY, bool draw, float scrollY) const;

    static const int kMaxNarrativeLines = 500;
    static const float kScrollbarWidth;

    const int screenWidth;
    const int screenHeight;

    RoomDatabase& roomDatabase;
    std::string currentRoomId;
    
    Texture2D locationImage;
    std::string baseDescription;
    std::string narrativeText;
    std::string examineDetails;
    Font descriptionFont;
    Font boldFont;
    
    bool forward;
    bool backward;
    bool left;
    bool right;

    float fontSize = 20;
    const bool wordWrap = true;
    const int spacing = 2;
    const Color textColor = DARKGRAY;
    const int xOffset = 14;
    const int yOffset = 14;

    const Rectangle textBox;
    const Rectangle buttonBox;
    ButtonMgr buttonMgr;

    float narrativeScrollY = 0.0f;
    mutable bool narrativeLayoutDirty = true;
    bool scrollbarDragging = false;
    float scrollbarDragOffsetY = 0.0f;

    mutable float narrativeContentHeight = 0.0f;
};

}

#endif /* LOCATION_H */