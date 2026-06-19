#include <Location.h>
#include <raylib.h>
#include <sstream>

namespace testgame
{
    Location::Location(const LocationStruct& locationStruct, Vector2 screenSize, RoomDatabase& roomDatabase, const std::string& roomId)
    : screenWidth((int)screenSize.x),
      screenHeight((int)screenSize.y),
      roomDatabase(roomDatabase),
      currentRoomId(roomId),
      locationImage(locationStruct.locationImage),
      baseDescription(locationStruct.locationDescription),
      narrativeText(locationStruct.locationDescription),
      examineDetails(locationStruct.examineDetails),
      descriptionFont(locationStruct.descriptionFont),
      boldFont(locationStruct.boldFont),
      forward(locationStruct.movementFilter.forward),
      backward(locationStruct.movementFilter.backward),
      left(locationStruct.movementFilter.left),
      right(locationStruct.movementFilter.right),
      textBox{screenWidth / 2.0f, 0, screenWidth / 2.0f, screenHeight * 2.0f / 3.0f},
      buttonBox{screenWidth / 2.0f, screenHeight * 2.0f / 3.0f, screenWidth / 2.0f, screenHeight / 3.0f},
      buttonMgr(buttonBox, descriptionFont)
    {
        buttonMgr.setAvailability(
            locationStruct.movementFilter,
            locationStruct.actionFilter);
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

    void Location::appendExamineDetails()
    {
        if (examineDetails.empty())
            return;

        narrativeText += "\n\nExamining:\n";
        narrativeText += examineDetails;
    }
    
    void Location::tryMove(const std::string& direction)
    {
        const std::string nextRoomId = roomDatabase.getExitRoomId(currentRoomId, direction);
        if (nextRoomId.empty())
            return;

        LocationStruct nextLocation;
        if (!roomDatabase.loadRoom(nextRoomId, nextLocation))
            return;

        UnloadTexture(locationImage);
        currentRoomId = nextRoomId;
        applyLocationStruct(nextLocation);
    }

    void Location::applyLocationStruct(const LocationStruct& locationStruct)
    {
        locationImage = locationStruct.locationImage;
        baseDescription = locationStruct.locationDescription;
        narrativeText = locationStruct.locationDescription;
        examineDetails = locationStruct.examineDetails;
        descriptionFont = locationStruct.descriptionFont;
        boldFont = locationStruct.boldFont;
        forward = locationStruct.movementFilter.forward;
        backward = locationStruct.movementFilter.backward;
        left = locationStruct.movementFilter.left;
        right = locationStruct.movementFilter.right;
        buttonMgr.setAvailability(
            locationStruct.movementFilter,
            locationStruct.actionFilter);
    }

    void Location::update()
    {
        buttonMgr.update();

        if (buttonMgr.consumeExamineButtonClick())
            appendExamineDetails();

        if (buttonMgr.consumeForwardButtonClick())
            tryMove("forward");
        if (buttonMgr.consumeBackwardButtonClick())
            tryMove("backward");
        if (buttonMgr.consumeLeftButtonClick())
            tryMove("left");
        if (buttonMgr.consumeRightButtonClick())
            tryMove("right");
    }

    void Location::draw() const
    {
        ClearBackground(BLACK);

        // Text Box
        DrawTexture(locationImage, 0, 0, WHITE);
        DrawRectangleLinesEx(textBox, 4, GRAY);
        drawNarrativeText();

        // Button Box
        buttonMgr.draw();
    }

    void Location::drawNarrativeText() const
    {
        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.empty())
            {
                textOffsetY += (descriptionFont.baseSize + descriptionFont.baseSize / 2.0f) *
                    (fontSize / (float)descriptionFont.baseSize) * 0.5f;
                continue;
            }

            const bool isExaminingHeader = (line == "Examining:");
            const Font lineFont = isExaminingHeader ? boldFont : descriptionFont;
            const float lineFontSize = fontSize;
            drawWrappedParagraph(line.c_str(), lineFont, lineFontSize, textOffsetY);
        }
    }

    void Location::drawWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY) const
    {
        int length = TextLength(text);

        float textOffsetX = 0.0f;
        float scaleFactor = paragraphFontSize / (float)font.baseSize;
        float lineHeight = (font.baseSize + font.baseSize / 2.0f) * scaleFactor;

        enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
        int state = wordWrap ? MEASURE_STATE : DRAW_STATE;

        int startLine = -1;
        int endLine = -1;
        int lastk = -1;

        for (int i = 0, k = 0; i < length; i++, k++)
        {
            int codepointByteCount = 0;
            int codepoint = GetCodepoint(&text[i], &codepointByteCount);
            int index = GetGlyphIndex(font, codepoint);

            if (codepoint == 0x3f) codepointByteCount = 1;
            i += (codepointByteCount - 1);

            float glyphWidth = 0;
            if (codepoint != '\n')
            {
                glyphWidth = (font.glyphs[index].advanceX == 0)
                    ? font.recs[index].width * scaleFactor
                    : font.glyphs[index].advanceX * scaleFactor;

                if (i + 1 < length) glyphWidth = glyphWidth + spacing;
            }

            if (state == MEASURE_STATE)
            {
                if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;

                if ((textOffsetX + glyphWidth) > (textBox.width - xOffset))
                {
                    endLine = (endLine < 1) ? i : endLine;
                    if (i == endLine) endLine -= codepointByteCount;
                    if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);

                    state = !state;
                }
                else if ((i + 1) == length)
                {
                    endLine = i;
                    state = !state;
                }
                else if (codepoint == '\n') state = !state;

                if (state == DRAW_STATE)
                {
                    textOffsetX = 0;
                    i = startLine;
                    glyphWidth = 0;

                    int tmp = lastk;
                    lastk = k - 1;
                    k = tmp;
                }
            }
            else
            {
                if (codepoint == '\n')
                {
                    if (!wordWrap)
                    {
                        textOffsetY += lineHeight;
                        textOffsetX = 0;
                    }
                }
                else
                {
                    if (!wordWrap && ((textOffsetX + glyphWidth) > (textBox.width - xOffset)))
                    {
                        textOffsetY += lineHeight;
                        textOffsetX = 0;
                    }

                    if ((textOffsetY + yOffset + font.baseSize * scaleFactor) > textBox.height) return;

                    if ((codepoint != ' ') && (codepoint != '\t'))
                    {
                        DrawTextCodepoint(
                            font,
                            codepoint,
                            (Vector2){ textBox.x + xOffset + textOffsetX, textBox.y + yOffset + textOffsetY },
                            paragraphFontSize,
                            textColor);
                    }
                }

                if (wordWrap && (i == endLine))
                {
                    textOffsetY += lineHeight;
                    textOffsetX = 0;
                    startLine = endLine;
                    endLine = -1;
                    glyphWidth = 0;
                    k = lastk;

                    state = !state;
                }
            }

            if ((textOffsetX != 0) || (codepoint != ' ')) textOffsetX += glyphWidth;
        }
    }

    void Location::DrawTextBoxed(const char *text) const
    {
        float textOffsetY = 0.0f;
        drawWrappedParagraph(text, descriptionFont, fontSize, textOffsetY);
    }

}