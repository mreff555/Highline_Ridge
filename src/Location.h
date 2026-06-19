#ifndef LOCATION_H
#define LOCATION_H

#include <LocationStruct.h>
#include <ButtonMgr.h>
#include <raylib.h>
#include <string>
namespace testgame
{

class Location
{
    public:
    Location(const LocationStruct& locationStruct, Vector2 screenSize);

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
    void update(const LocationStruct& locationStruct);
    void DrawTextBoxed(const char* text) const;
    void drawNarrativeText() const;
    void drawWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY) const;
    void appendExamineDetails();

    private:
    const int screenWidth;
    const int screenHeight;
    
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

    // TODO: class for observations and items
};

}

#endif /* LOCATION_H */