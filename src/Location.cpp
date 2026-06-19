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

    const char* kSaloonPatronLines[] = {
        "A faro player at the stove table looks up long enough to fan his cards. \"House always remembers,\" he says, and returns to his stack as though you are part of the furniture.",
        "On the balcony, a woman in burgundy silk follows your movement with professional interest. \"Back early,\" she murmurs, and the words are not quite a question.",
        "The piano player cuts a glance your way mid-phrase, fingers never stopping. \"Easy now,\" he says to the keys, but you suspect the advice is meant for you.",
        "A miner at the poker table rubs a thumb over a chipped ace. \"Luck ain't a debt you can outrun,\" he mutters, and the table laughs without warmth.",
        "A barmaid passes with an empty tray, eyes flicking over you once. \"Mind the stairs,\" she says, already gone into the crowd."
    };
    const int kSaloonPatronLineCount = sizeof(kSaloonPatronLines) / sizeof(kSaloonPatronLines[0]);
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
      buttonMgr(buttonBox, descriptionFont)
    {
        trimNarrativeBuffer();
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

    float Location::getNarrativeVisibleHeight() const
    {
        return textBox.height - yOffset * 2.0f;
    }

    float Location::getNarrativeWrapWidth() const
    {
        return textBox.width - xOffset - kScrollbarWidth - 8.0f;
    }

    void Location::trimNarrativeBuffer()
    {
        std::vector<std::string> lines;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
            lines.push_back(line);

        if ((int)lines.size() <= kMaxNarrativeLines)
            return;

        lines.erase(lines.begin(), lines.begin() + (lines.size() - kMaxNarrativeLines));

        std::ostringstream rebuilt;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
                rebuilt << '\n';
            rebuilt << lines[i];
        }

        narrativeText = rebuilt.str();
        narrativeLayoutDirty = true;
    }

    bool Location::isBoldNarrativeHeader(const std::string& line) const
    {
        return line == "Examining:" || line == "Speaking:" || line == "Using:";
    }

    bool Location::isDialogChoiceLine(const std::string& line) const
    {
        return line.size() > 2 && line.compare(0, 2, "> ") == 0;
    }

    Color Location::narrativeLineColor(const std::string& line) const
    {
        if (!awaitingDialogChoice || !isDialogChoiceLine(line))
            return textColor;

        for (const DialogChoice& choice : pendingDialogChoices)
        {
            if (line == choice.lineText)
            {
                const Vector2 mousePos = GetMousePosition();
                for (const NarrativeChoiceHitArea& hitArea : narrativeChoiceHitAreas)
                {
                    if (hitArea.id == choice.id && CheckCollisionPointRec(mousePos, hitArea.bounds))
                        return kChoiceHover;
                }
                return kChoiceText;
            }
        }

        return textColor;
    }

    void Location::appendNarrativeSection(const char* header, const std::string& details)
    {
        if (details.empty())
            return;

        narrativeText += "\n\n";
        narrativeText += header;
        narrativeText += "\n";
        narrativeText += details;
        trimNarrativeBuffer();
        narrativeLayoutDirty = true;
    }

    void Location::appendExamineDetails()
    {
        if (examineDetails.empty())
            return;

        appendNarrativeSection("Examining:", examineDetails);
        hasExaminedCurrentRoom = true;
        updateActionAvailability();
    }

    void Location::appendSpeakDetails()
    {
        if (speakDetails.empty() || hasSpokenInCurrentRoom)
            return;

        appendNarrativeSection("Speaking:", speakDetails);
        hasSpokenInCurrentRoom = true;
        updateActionAvailability();
    }

    void Location::appendDialogChoices(const std::vector<DialogChoice>& choices)
    {
        pendingDialogChoices = choices;
        awaitingDialogChoice = true;

        for (const DialogChoice& choice : choices)
        {
            narrativeText += "\n";
            narrativeText += choice.lineText;
        }

        trimNarrativeBuffer();
        narrativeLayoutDirty = true;
        updateActionAvailability();
    }

    void Location::beginBartenderConversation()
    {
        const std::string intro =
            "You step to the bar. The noise closes around you, then parts. The bartender meets your eyes "
            "with a neutral expression that gives away nothing. He knows you. That much is plain. He is not "
            "inclined to tell a story about it.\n\n"
            "\"You want the usual?\" he says, rag finally moving again across the wood.";

        appendNarrativeSection("Speaking:", intro);
        appendDialogChoices({
            {"water", "> No, just a water"},
            {"usual", "> Sure"}
        });
    }

    void Location::resolveDialogChoice(const std::string& choiceId)
    {
        if (!awaitingDialogChoice)
            return;

        awaitingDialogChoice = false;
        pendingDialogChoices.clear();
        narrativeChoiceHitAreas.clear();
        bartenderConversationComplete = true;

        if (choiceId == "water")
        {
            appendNarrativeSection(
                "Speaking:",
                "The bartender holds your gaze a beat longer than necessary, then turns away for a glass. "
                "He sets it on the bar without ceremony. Water, clear and cold. Under his breath, barely "
                "audible above the piano, he mutters that he is perfectly all right with you not drinking "
                "in his establishment. The words are flat. Not cruel. Not kind. Simply settled.\n\n"
                "You lift the glass and drink. The water cuts clean through the smoke in your throat and "
                "the ash at the back of your tongue. Your head steadies. The room feels a fraction less "
                "unreal, as though someone has wiped a smear from a window you did not know was dirty.");

            if (tryApplyStatusDeltas("saloon_interior:water", 5.0f, 0.0f, 0.0f, 10.0f, false))
                updateActionAvailability();
        }
        else if (choiceId == "usual")
        {
            appendNarrativeSection(
                "Speaking:",
                "The bartender's jaw tightens almost imperceptibly. He reaches high above the backbar, "
                "to a shelf you would need a ladder to trust, and brings down a bottle of bourbon with "
                "an artistic label and a wax seal dark as blood. He pours a single shot without measuring, "
                "slides it across the pine, and watches you with a look that could crush stone.\n\n"
                "You down it anyway. Fire and oak. The room tilts half a degree and rights itself. Your "
                "resolve hardens even as your thoughts go slippery at the edges.");

            if (tryApplyStatusDeltas("saloon_interior:usual", 0.0f, 0.0f, 20.0f, -10.0f, false))
            {
                updateActionAvailability();

                if (lucidity <= 0.0f)
                {
                    applyLucidityCollapseRestart();
                    return;
                }
            }
        }

        updateActionAvailability();
    }

    const char* Location::pickRandomSaloonPatronLine()
    {
        if (kSaloonPatronLineCount <= 0)
            return "";

        int index = rand() % kSaloonPatronLineCount;
        if (kSaloonPatronLineCount > 1)
        {
            int guard = 0;
            while (index == lastSaloonPatronIndex && guard < 8)
            {
                index = rand() % kSaloonPatronLineCount;
                ++guard;
            }
        }

        lastSaloonPatronIndex = index;
        return kSaloonPatronLines[index];
    }

    void Location::handleSaloonSpeak()
    {
        if (awaitingDialogChoice)
            return;

        if (!bartenderConversationComplete)
        {
            beginBartenderConversation();
            ++saloonSpeakCount;
            return;
        }

        const char* patronLine = pickRandomSaloonPatronLine();
        if (patronLine[0] != '\0')
            appendNarrativeSection("Speaking:", patronLine);

        ++saloonSpeakCount;
    }

    void Location::handleSpeak()
    {
        if (currentRoomId == "saloon_interior")
        {
            handleSaloonSpeak();
            return;
        }

        appendSpeakDetails();
    }

    void Location::applyLucidityCollapseRestart()
    {
        LocationStruct cabinLocation;
        std::string startRoomId;
        if (!roomDatabase.loadStartRoom(cabinLocation, startRoomId))
        {
            TraceLog(LOG_ERROR, "Failed to restart at cabin after lucidity collapse");
            return;
        }

        UnloadTexture(locationImage);
        currentRoomId = startRoomId;
        applyLocationStruct(cabinLocation);

        narrativeText = std::string(kWakeOnFloorPrefix) + "\n\n" + baseDescription;
        health = 90.0f;
        energy = 20.0f;
        tenacity = 50.0f;
        lucidity = 30.0f;
        narrativeScrollY = 0.0f;
        narrativeLayoutDirty = true;
        trimNarrativeBuffer();
        updateActionAvailability();
    }

    void Location::handleNarrativeChoiceInput()
    {
        if (!awaitingDialogChoice || narrativeChoiceHitAreas.empty())
            return;

        const Rectangle textArea = {
            textBox.x,
            textBox.y,
            textBox.width - kScrollbarWidth,
            textBox.height
        };

        const Vector2 mousePos = GetMousePosition();
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !CheckCollisionPointRec(mousePos, textArea))
            return;

        for (const NarrativeChoiceHitArea& hitArea : narrativeChoiceHitAreas)
        {
            if (CheckCollisionPointRec(mousePos, hitArea.bounds))
            {
                resolveDialogChoice(hitArea.id);
                scrollNarrativeToHeader("Speaking:");
                return;
            }
        }
    }

    bool Location::tryApplyStatusDeltas(
        const std::string& actionKey,
        float healthDelta,
        float energyDelta,
        float tenacityDelta,
        float lucidityDelta,
        bool allowRepeat)
    {
        const bool hasAnyDelta = healthDelta != 0.0f || energyDelta != 0.0f
            || tenacityDelta != 0.0f || lucidityDelta != 0.0f;
        if (!hasAnyDelta)
            return false;

        if (!allowRepeat && consumedStatusActions.count(actionKey) > 0)
            return false;

        health = std::min(100.0f, health + healthDelta);
        energy = std::min(100.0f, energy + energyDelta);
        tenacity = std::min(100.0f, tenacity + tenacityDelta);
        lucidity = std::min(100.0f, std::max(0.0f, lucidity + lucidityDelta));

        if (!allowRepeat)
            consumedStatusActions.insert(actionKey);

        return true;
    }

    void Location::appendUseDetails()
    {
        if (useDetails.empty() || hasUsedInCurrentRoom)
            return;

        appendNarrativeSection("Using:", useDetails);
        tryApplyStatusDeltas(
            currentRoomId + ":use",
            useHealthDelta,
            useEnergyDelta,
            0.0f,
            0.0f,
            useRepeatStatus);
        hasUsedInCurrentRoom = true;
        updateActionAvailability();
    }

    void Location::updateActionAvailability()
    {
        MovementStruct movement;
        movement.up = up;
        movement.down = down;
        movement.forward = forward;
        movement.backward = backward;
        movement.left = left;
        movement.right = right;

        ActionStruct actions = baseActionFilter;
        if (currentRoomId == "saloon_interior")
            actions.speak = baseActionFilter.speak && !awaitingDialogChoice;
        else if (!speakDetails.empty())
            actions.speak = !hasSpokenInCurrentRoom;
        else
            actions.speak = false;

        if (!useDetails.empty())
            actions.use = hasExaminedCurrentRoom && !hasUsedInCurrentRoom;
        else
            actions.use = false;

        buttonMgr.setAvailability(movement, actions);
        buttonMgr.setStatus(health, energy, tenacity, lucidity);
    }

    float Location::getNarrativeLineOffsetY(const std::string& lineText, bool lastOccurrence) const
    {
        float textOffsetY = 0.0f;
        float foundOffset = -1.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (line == lineText)
                foundOffset = textOffsetY;
            else if (!lastOccurrence && foundOffset >= 0.0f)
            {
                narrativeContentHeight = textOffsetY;
                narrativeLayoutDirty = false;
                return foundOffset;
            }

            const Font lineFont = isBoldNarrativeHeader(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }

        narrativeContentHeight = textOffsetY;
        narrativeLayoutDirty = false;
        return foundOffset;
    }

    void Location::scrollNarrativeToHeader(const char* header)
    {
        const float headerOffset = getNarrativeLineOffsetY(header, true);
        if (headerOffset < 0.0f)
            return;

        const float maxScroll = std::max(0.0f, narrativeContentHeight - getNarrativeVisibleHeight());
        narrativeScrollY = std::max(0.0f, std::min(headerOffset, maxScroll));
    }
    
    void Location::tryMove(const std::string& direction)
    {
        const std::string nextRoomId = roomDatabase.getExitRoomId(currentRoomId, direction);
        if (nextRoomId.empty())
        {
            TraceLog(LOG_WARNING, "No exit '%s' from room '%s'", direction.c_str(), currentRoomId.c_str());
            return;
        }

        LocationStruct nextLocation;
        if (!roomDatabase.loadRoom(nextRoomId, nextLocation))
        {
            TraceLog(LOG_ERROR, "Failed to load room '%s'", nextRoomId.c_str());
            return;
        }

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
        speakDetails = locationStruct.speakDetails;
        useDetails = locationStruct.useDetails;
        useHealthDelta = locationStruct.useHealthDelta;
        useEnergyDelta = locationStruct.useEnergyDelta;
        useRepeatStatus = locationStruct.useRepeatStatus;
        baseActionFilter = locationStruct.actionFilter;
        descriptionFont = locationStruct.descriptionFont;
        boldFont = locationStruct.boldFont;
        up = locationStruct.movementFilter.up;
        down = locationStruct.movementFilter.down;
        forward = locationStruct.movementFilter.forward;
        backward = locationStruct.movementFilter.backward;
        left = locationStruct.movementFilter.left;
        right = locationStruct.movementFilter.right;
        hasExaminedCurrentRoom = false;
        hasSpokenInCurrentRoom = false;
        hasUsedInCurrentRoom = false;
        awaitingDialogChoice = false;
        pendingDialogChoices.clear();
        narrativeChoiceHitAreas.clear();

        if (currentRoomId == "saloon_interior")
        {
            saloonSpeakCount = 0;
            bartenderConversationComplete = false;
            lastSaloonPatronIndex = -1;
        }

        narrativeScrollY = 0.0f;
        narrativeLayoutDirty = true;
        trimNarrativeBuffer();
        updateActionAvailability();
    }

    void Location::rebuildNarrativeLayout() const
    {
        narrativeContentHeight = 0.0f;

        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            const Font lineFont = isBoldNarrativeHeader(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }

        narrativeContentHeight = textOffsetY;
        narrativeLayoutDirty = false;
    }

    void Location::handleNarrativeScrollInput()
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        const float visibleHeight = getNarrativeVisibleHeight();
        const float maxScroll = std::max(0.0f, narrativeContentHeight - visibleHeight);
        const float lineHeight = getNarrativeLineHeight();

        const Rectangle scrollTrack = {
            textBox.x + textBox.width - kScrollbarWidth,
            textBox.y + yOffset,
            kScrollbarWidth,
            visibleHeight
        };

        const float thumbHeight = (narrativeContentHeight <= 0.0f)
            ? visibleHeight
            : std::max(24.0f, visibleHeight * (visibleHeight / narrativeContentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (maxScroll > 0.0f
            ? (narrativeScrollY / maxScroll) * thumbTravel
            : 0.0f);

        const Rectangle scrollThumb = { scrollTrack.x + 2.0f, thumbY, scrollTrack.width - 4.0f, thumbHeight };
        const Vector2 mousePos = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mousePos, scrollThumb))
            {
                scrollbarDragging = true;
                scrollbarDragOffsetY = mousePos.y - scrollThumb.y;
            }
            else if (CheckCollisionPointRec(mousePos, scrollTrack) && thumbTravel > 0.0f)
            {
                narrativeScrollY = ((mousePos.y - scrollTrack.y - thumbHeight * 0.5f) / thumbTravel) * maxScroll;
            }
        }

        if (scrollbarDragging)
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && thumbTravel > 0.0f)
                narrativeScrollY = ((mousePos.y - scrollTrack.y - scrollbarDragOffsetY) / thumbTravel) * maxScroll;
            else
                scrollbarDragging = false;
        }

        const Rectangle textScrollArea = {
            textBox.x,
            textBox.y,
            textBox.width - kScrollbarWidth,
            textBox.height
        };

        if (CheckCollisionPointRec(mousePos, textScrollArea))
            narrativeScrollY -= GetMouseWheelMove() * lineHeight * 3.0f;

        narrativeScrollY = std::max(0.0f, std::min(narrativeScrollY, maxScroll));

        if (narrativeContentHeight > visibleHeight && narrativeScrollY >= maxScroll - 1.0f)
            narrativeScrollY = maxScroll;
    }

    void Location::update()
    {
        handleNarrativeChoiceInput();
        buttonMgr.update();

        if (buttonMgr.consumeUpButtonClick())
            tryMove("up");
        else if (buttonMgr.consumeDownButtonClick())
            tryMove("down");
        else if (buttonMgr.consumeForwardButtonClick())
            tryMove("forward");
        else if (buttonMgr.consumeBackwardButtonClick())
            tryMove("backward");
        else if (buttonMgr.consumeLeftButtonClick())
            tryMove("left");
        else if (buttonMgr.consumeRightButtonClick())
            tryMove("right");

        if (buttonMgr.consumeExamineButtonClick())
        {
            appendExamineDetails();
            scrollNarrativeToHeader("Examining:");
        }

        if (buttonMgr.consumeSpeakButtonClick())
        {
            handleSpeak();
            scrollNarrativeToHeader("Speaking:");
        }

        if (buttonMgr.consumeUseButtonClick())
        {
            appendUseDetails();
            scrollNarrativeToHeader("Using:");
        }

        handleNarrativeScrollInput();
    }

    void Location::draw() const
    {
        ClearBackground(BLACK);

        DrawTexture(locationImage, 0, 0, WHITE);
        DrawRectangleLinesEx(textBox, 4, GRAY);
        drawNarrativeText();
        drawNarrativeScrollbar();

        buttonMgr.draw();
    }

    void Location::drawNarrativeScrollbar() const
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        const float visibleHeight = getNarrativeVisibleHeight();
        const float maxScroll = std::max(0.0f, narrativeContentHeight - visibleHeight);

        const Rectangle scrollTrack = {
            textBox.x + textBox.width - kScrollbarWidth,
            textBox.y + yOffset,
            kScrollbarWidth,
            visibleHeight
        };

        DrawRectangleRec(scrollTrack, kScrollTrack);

        if (maxScroll <= 0.0f)
            return;

        const float thumbHeight = std::max(24.0f, visibleHeight * (visibleHeight / narrativeContentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (narrativeScrollY / maxScroll) * thumbTravel;

        const Rectangle scrollThumb = {
            scrollTrack.x + 2.0f,
            thumbY,
            scrollTrack.width - 4.0f,
            thumbHeight
        };

        const bool thumbHovered = CheckCollisionPointRec(GetMousePosition(), scrollThumb);
        DrawRectangleRounded(scrollThumb, 0.4f, 6, thumbHovered ? kScrollThumbHover : kScrollThumb);
    }

    void Location::drawNarrativeText() const
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        narrativeChoiceHitAreas.clear();

        const Rectangle clipArea = {
            textBox.x,
            textBox.y,
            textBox.width - kScrollbarWidth,
            textBox.height
        };

        BeginScissorMode(
            (int)clipArea.x,
            (int)clipArea.y,
            (int)clipArea.width,
            (int)clipArea.height);

        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            const Font lineFont = isBoldNarrativeHeader(line) ? boldFont : descriptionFont;
            const Color lineColor = narrativeLineColor(line);

            if (awaitingDialogChoice && isDialogChoiceLine(line))
            {
                for (const DialogChoice& choice : pendingDialogChoices)
                {
                    if (line != choice.lineText)
                        continue;

                    const float drawY = textBox.y + yOffset + textOffsetY - narrativeScrollY;
                    const Vector2 textSize = MeasureTextEx(lineFont, line.c_str(), fontSize, spacing);
                    narrativeChoiceHitAreas.push_back({
                        choice.id,
                        { textBox.x + xOffset, drawY, textSize.x, getNarrativeLineHeight() }
                    });
                    break;
                }
            }

            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, true, narrativeScrollY, lineColor);
        }

        EndScissorMode();
    }

    void Location::layoutWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY, bool draw, float scrollY, Color lineColor) const
    {
        int length = TextLength(text);

        float textOffsetX = 0.0f;
        float scaleFactor = paragraphFontSize / (float)font.baseSize;
        float lineHeight = (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
        const float wrapWidth = getNarrativeWrapWidth();
        const float visibleHeight = getNarrativeVisibleHeight();

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

                if ((textOffsetX + glyphWidth) > wrapWidth)
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
                    if (!wordWrap && ((textOffsetX + glyphWidth) > wrapWidth))
                    {
                        textOffsetY += lineHeight;
                        textOffsetX = 0;
                    }

                    const float drawY = textBox.y + yOffset + textOffsetY - scrollY;
                    const bool visible = (textOffsetY + lineHeight > scrollY) &&
                                         (textOffsetY < scrollY + visibleHeight);

                    if (draw && visible && (codepoint != ' ') && (codepoint != '\t'))
                    {
                        DrawTextCodepoint(
                            font,
                            codepoint,
                            (Vector2){ textBox.x + xOffset + textOffsetX, drawY },
                            paragraphFontSize,
                            lineColor);
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

}