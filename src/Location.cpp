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
    const Color kPaperShadow = {118, 98, 68, 255};
    const Color kPaperEdge = {108, 88, 58, 255};
    const Color kRuleLine = {132, 148, 168, 85};
    const Color kMarginLine = {168, 78, 68, 150};
    const Color kBindingRing = {98, 82, 62, 255};
    const Color kBindingHole = {58, 48, 38, 255};
    const Color kScrollTrack = {206, 186, 148, 255};
    const Color kScrollThumb = {176, 148, 108, 255};
    const Color kScrollThumbHover = {148, 118, 78, 255};
    const Color kChoiceText = {92, 52, 22, 255};
    const Color kChoiceHover = {148, 88, 28, 255};

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
      ownsLocationImage(locationStruct.ownsLocationImage),
      isUnderConstruction(locationStruct.isUnderConstruction),
      baseDescription(locationStruct.locationDescription),
      narrativeText(locationStruct.locationDescription),
      examineDetails(locationStruct.examineDetails),
      examineFlag(locationStruct.examineFlag),
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
      buttonMgr(buttonBox, locationStruct.uiFont)
    {
        inventoryMgr.setFont(locationStruct.uiFont);
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
        if (notebookPaperTextureReady)
            UnloadTexture(notebookPaperTexture);

        if (ownsLocationImage && locationImage.id != 0)
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

    void Location::ensureNotebookPaperTexture() const
    {
        if (notebookPaperTextureReady)
            return;

        const int width = std::max(1, (int)textBox.width);
        const int height = std::max(1, (int)fullDialogHeight);
        Image paper = GenImageColor(width, height, {214, 188, 136, 255});

        for (int y = 0; y < height; ++y)
        {
            const float rowFade = (float)y / (float)height;
            for (int x = 0; x < width; ++x)
            {
                const float colFade = (float)x / (float)width;
                const unsigned int hash = (unsigned int)(x * 92837111u + y * 689287499u);

                int red = 208 - (int)(rowFade * 28.0f) - (int)(colFade * 8.0f);
                int green = 178 - (int)(rowFade * 24.0f) - (int)(colFade * 6.0f);
                int blue = 118 - (int)(rowFade * 18.0f) - (int)(colFade * 4.0f);

                if ((hash % 19u) == 0u)
                {
                    red -= 16;
                    green -= 18;
                    blue -= 12;
                }
                else if ((hash % 29u) == 0u)
                {
                    red += 10;
                    green += 8;
                    blue += 4;
                }

                if ((hash % 41u) == 0u)
                {
                    red -= 8;
                    green -= 10;
                    blue -= 6;
                }

                const int edgeX = std::min(x, width - 1 - x);
                const int edgeY = std::min(y, height - 1 - y);
                const int edge = std::min(edgeX, edgeY);
                if (edge < 36)
                {
                    const int edgeWeight = 36 - edge;
                    red -= edgeWeight / 3;
                    green -= edgeWeight / 2;
                    blue -= edgeWeight / 4;
                }

                red = std::max(0, std::min(255, red));
                green = std::max(0, std::min(255, green));
                blue = std::max(0, std::min(255, blue));
                ImageDrawPixel(&paper, x, y, { (unsigned char)red, (unsigned char)green, (unsigned char)blue, 255 });
            }
        }

        for (int y = 0; y < height; y += 3)
        {
            const unsigned int rowHash = (unsigned int)(y * 2654435761u);
            const int shift = (int)(rowHash % 7u) - 3;
            for (int x = 0; x < width; x += 2)
            {
                const unsigned int hash = rowHash ^ (unsigned int)(x * 1597334677u);
                if ((hash % 11u) > 1u)
                    continue;

                const int tone = 18 + (int)(hash % 24u);
                ImageDrawPixel(
                    &paper,
                    x,
                    y,
                    { (unsigned char)(118 + shift), (unsigned char)(98 + shift), (unsigned char)(62 + shift), (unsigned char)tone });
            }
        }

        notebookPaperTexture = LoadTextureFromImage(paper);
        UnloadImage(paper);
        notebookPaperTextureReady = notebookPaperTexture.id != 0;
    }

    void Location::drawNotebookBackdrop(const Rectangle& bounds) const
    {
        ensureNotebookPaperTexture();

        const Rectangle shadow = {
            bounds.x + 6.0f,
            bounds.y + 8.0f,
            bounds.width,
            bounds.height
        };
        DrawRectangleRec(shadow, kPaperShadow);

        if (notebookPaperTextureReady)
        {
            DrawTexturePro(
                notebookPaperTexture,
                { 0.0f, 0.0f, (float)notebookPaperTexture.width, bounds.height },
                bounds,
                { 0.0f, 0.0f },
                0.0f,
                WHITE);
        }
        else
        {
            DrawRectangleRec(bounds, {208, 182, 132, 255});
        }

        const float lineStep = getNarrativeLineHeight();
        const float firstLineY = bounds.y + yOffset + lineStep * 0.35f;
        for (float ruleY = firstLineY; ruleY < bounds.y + bounds.height - 18.0f; ruleY += lineStep)
        {
            DrawLineEx(
                { bounds.x + 18.0f, ruleY },
                { bounds.x + bounds.width - 24.0f, ruleY },
                1.0f,
                kRuleLine);
        }

        const float marginX = bounds.x + 58.0f;
        DrawLineEx(
            { marginX, bounds.y + 16.0f },
            { marginX, bounds.y + bounds.height - 16.0f },
            2.0f,
            kMarginLine);

        const Rectangle bindingStrip = {
            bounds.x,
            bounds.y,
            18.0f,
            bounds.height
        };
        DrawRectangleRec(bindingStrip, {176, 152, 108, 255});

        const float holeRadius = 5.5f;
        const float holeSpacing = 42.0f;
        for (float holeY = bounds.y + 34.0f; holeY < bounds.y + bounds.height - 34.0f; holeY += holeSpacing)
        {
            const Vector2 holeCenter = { bounds.x + 9.0f, holeY };
            DrawCircleV(holeCenter, holeRadius + 1.5f, kBindingRing);
            DrawCircleV(holeCenter, holeRadius, kBindingHole);
            DrawCircleV({ holeCenter.x - 1.0f, holeCenter.y - 1.0f }, holeRadius - 2.0f, {78, 66, 52, 255});
        }

        DrawTextEx(
            boldFont,
            "CASE NOTES",
            { bounds.x + 26.0f, bounds.y + 12.0f },
            18.0f,
            1.0f,
            Fade({98, 72, 48, 255}, 0.62f));

        const float foldSize = 28.0f;
        DrawTriangle(
            { bounds.x + bounds.width, bounds.y },
            { bounds.x + bounds.width - foldSize, bounds.y },
            { bounds.x + bounds.width, bounds.y + foldSize },
            {184, 158, 112, 255});
        DrawLineEx(
            { bounds.x + bounds.width - foldSize, bounds.y },
            { bounds.x + bounds.width, bounds.y + foldSize },
            1.0f,
            kPaperEdge);

        DrawRectangleLinesEx(bounds, 2.0f, kPaperEdge);

        const Rectangle scrollbarGutter = {
            bounds.x + bounds.width - kScrollbarWidth,
            bounds.y,
            kScrollbarWidth,
            bounds.height
        };
        DrawRectangleRec(scrollbarGutter, {196, 174, 132, 255});
        DrawLineEx(
            { scrollbarGutter.x, scrollbarGutter.y + 8.0f },
            { scrollbarGutter.x, scrollbarGutter.y + scrollbarGutter.height - 8.0f },
            1.0f,
            Fade(kPaperEdge, 0.8f));
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

        if (locationImage.id != 0)
        {
            DrawTexturePro(
                locationImage,
                { 0.0f, 0.0f, (float)locationImage.width, (float)locationImage.height },
                mainBounds,
                { 0.0f, 0.0f },
                0.0f,
                WHITE);
        }
        else
        {
            DrawRectangleRec(mainBounds, (Color){48, 44, 56, 255});
            const char* fallback = "UNDER CONSTRUCTION";
            const float fontSize = 36.0f;
            const Vector2 size = MeasureTextEx(descriptionFont, fallback, fontSize, 2.0f);
            DrawTextEx(
                descriptionFont,
                fallback,
                {
                    mainBounds.x + (mainBounds.width - size.x) * 0.5f,
                    mainBounds.y + (mainBounds.height - size.y) * 0.5f
                },
                fontSize,
                2.0f,
                (Color){186, 150, 72, 255});
        }
    }

    Rectangle Location::getDialogBounds() const
    {
        const float height = inventoryMgr.isOpen() ? fullDialogHeight * 0.5f : fullDialogHeight;
        return { textBox.x, textBox.y, textBox.width, height };
    }

    Rectangle Location::getInventoryPanelBounds() const
    {
        const Rectangle dialog = getDialogBounds();
        return {
            textBox.x,
            dialog.y + dialog.height,
            textBox.width,
            fullDialogHeight * 0.5f
        };
    }

    void Location::updateInventoryLayout()
    {
        if (inventoryMgr.isOpen())
            inventoryMgr.setPanelBounds(getInventoryPanelBounds());
    }

    float Location::getNarrativeVisibleHeight() const
    {
        return getDialogBounds().height - yOffset * 2.0f;
    }

    float Location::getNarrativeWrapWidth() const
    {
        return getDialogBounds().width - xOffset - kScrollbarWidth - 8.0f;
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

    bool Location::isBoldNarrativeLine(const std::string& line) const
    {
        return isBoldNarrativeHeader(line)
            || (awaitingDialogChoice && isDialogChoiceLine(line));
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
        if (!examineFlag.empty())
            storyFlags.insert(examineFlag);
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

        if (!choices.empty())
            scrollNarrativeToLine(choices.front().lineText, false);
    }

    void Location::applyStatusEffects(const std::vector<StatusEffect>& effects)
    {
        bool statsChanged = false;

        for (const StatusEffect& effect : effects)
        {
            if (!effect.hasDelta())
                continue;

            if (tryApplyStatusDeltas(
                    effect.key,
                    effect.health,
                    effect.energy,
                    effect.tenacity,
                    effect.lucidity,
                    effect.repeat))
            {
                statsChanged = true;

                if (effect.onZeroLucidity == "restart_cabin" && lucidity <= 0.0f)
                {
                    if (statsChanged)
                        updateActionAvailability();
                    applyLucidityCollapseRestart();
                    return;
                }
            }
        }

        if (statsChanged)
            updateActionAvailability();
    }

    void Location::processSpeakResult(const SpeakResult& result)
    {
        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            return;

        if (!result.narrative.empty())
            appendNarrativeSection("Speaking:", result.narrative);

        if (result.action == SpeakResult::Action::ShowChoices)
        {
            std::vector<DialogChoice> uiChoices;
            uiChoices.reserve(result.choices.size());
            for (const ConversationChoiceDef& choice : result.choices)
                uiChoices.push_back({ choice.id, choice.label });

            appendDialogChoices(uiChoices);
            return;
        }

        applyStatusEffects(result.statusEffects);
    }

    void Location::resolveDialogChoice(const std::string& choiceId)
    {
        if (!conversationMgr.isAwaitingChoice() && !awaitingDialogChoice)
            return;

        SpeakResult result = conversationMgr.resolveChoice(choiceId);
        if (result.action == SpeakResult::Action::None && result.narrative.empty())
        {
            const RoomSpeakConfig& speakConfig = roomDatabase.getSpeakConfig(currentRoomId);
            result = conversationMgr.resolveChoiceFromConfig(speakConfig, choiceId);
        }

        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            return;

        awaitingDialogChoice = false;
        pendingDialogChoices.clear();
        narrativeChoiceHitAreas.clear();

        processSpeakResult(result);
        updateActionAvailability();
    }

    void Location::handleSpeak()
    {
        const RoomSpeakConfig& speakConfig = roomDatabase.getSpeakConfig(currentRoomId);
        if (speakConfig.hasPhases())
        {
            SpeakResult result = conversationMgr.handleSpeak(
                currentRoomId,
                speakConfig,
                storyFlags);
            processSpeakResult(result);
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

        if (ownsLocationImage && locationImage.id != 0)
            UnloadTexture(locationImage);
        previousRoomId.clear();
        currentRoomId = startRoomId;
        applyLocationStruct(cabinLocation);

        narrativeText += "\n\n";
        narrativeText += kWakeOnFloorPrefix;
        narrativeText += "\n\n";
        narrativeText += baseDescription;
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
        if (!awaitingDialogChoice)
            return;

        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        rebuildNarrativeChoiceHitAreas();

        if (narrativeChoiceHitAreas.empty())
            return;

        const Rectangle dialog = getDialogBounds();
        const Rectangle textArea = {
            dialog.x,
            dialog.y,
            dialog.width - kScrollbarWidth,
            dialog.height
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
        MovementStruct movement{};
        ActionStruct actions{};

        if (inventoryMgr.isOpen())
        {
            movement.up = false;
            movement.down = false;
            movement.forward = false;
            movement.left = false;
            movement.right = false;
            movement.backward = false;

            if (inventoryMgr.isExaminingItem())
                movement.backward = true;
            else if (inventoryMgr.canExamineSelectedItem())
                actions.examine = true;
        }
        else if (isUnderConstruction)
        {
            movement.backward = !previousRoomId.empty();
        }
        else
        {
            movement.up = up;
            movement.down = down;
            movement.forward = forward;
            movement.backward = backward;
            movement.left = left;
            movement.right = right;

            if (currentRoomId == "saloon_interior" && !hasExaminedCurrentRoom)
            {
                movement.up = false;
                movement.right = false;
            }

            actions = baseActionFilter;
            const RoomSpeakConfig& speakConfig = roomDatabase.getSpeakConfig(currentRoomId);
            if (speakConfig.hasPhases())
                actions.speak = conversationMgr.canSpeak(
                    speakConfig,
                    baseActionFilter.speak,
                    storyFlags);
            else if (!speakDetails.empty())
                actions.speak = !hasSpokenInCurrentRoom;
            else
                actions.speak = false;

            if (!useDetails.empty())
                actions.use = hasExaminedCurrentRoom && !hasUsedInCurrentRoom;
            else
                actions.use = false;
        }

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

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }

        narrativeContentHeight = textOffsetY;
        narrativeLayoutDirty = false;
        return foundOffset;
    }

    void Location::scrollNarrativeToHeader(const char* header)
    {
        scrollNarrativeToLine(header, true);
    }

    void Location::scrollNarrativeToLine(const std::string& lineText, bool lastOccurrence)
    {
        const float lineOffset = getNarrativeLineOffsetY(lineText, lastOccurrence);
        if (lineOffset < 0.0f)
            return;

        const float maxScroll = std::max(0.0f, narrativeContentHeight - getNarrativeVisibleHeight());
        narrativeScrollY = std::max(0.0f, std::min(lineOffset, maxScroll));
    }

    void Location::rebuildNarrativeChoiceHitAreas() const
    {
        narrativeChoiceHitAreas.clear();

        if (!awaitingDialogChoice || pendingDialogChoices.empty())
            return;

        const Rectangle dialog = getDialogBounds();

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

            if (isDialogChoiceLine(line))
            {
                for (const DialogChoice& choice : pendingDialogChoices)
                {
                    if (line != choice.lineText)
                        continue;

                    const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
                    float measureY = textOffsetY;
                    layoutWrappedParagraph(line.c_str(), lineFont, fontSize, measureY, false, 0.0f, textColor);
                    const float choiceHeight = std::max(getNarrativeLineHeight(), measureY - textOffsetY);
                    const float drawY = dialog.y + yOffset + textOffsetY - narrativeScrollY;

                    narrativeChoiceHitAreas.push_back({
                        choice.id,
                        { dialog.x + xOffset, drawY, getNarrativeWrapWidth(), choiceHeight }
                    });
                    break;
                }
            }

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }
    }
    
    void Location::tryMove(const std::string& direction)
    {
        if (isUnderConstruction)
        {
            if (direction != "backward" || previousRoomId.empty())
                return;

            LocationStruct previousLocation;
            if (!roomDatabase.loadRoom(previousRoomId, previousLocation))
            {
                TraceLog(LOG_ERROR, "Failed to load previous room '%s'", previousRoomId.c_str());
                return;
            }

            if (ownsLocationImage && locationImage.id != 0)
                UnloadTexture(locationImage);

            const std::string returnRoomId = previousRoomId;
            previousRoomId.clear();
            currentRoomId = returnRoomId;
            applyLocationStruct(previousLocation);
            return;
        }

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

        if (ownsLocationImage && locationImage.id != 0)
            UnloadTexture(locationImage);

        previousRoomId = currentRoomId;
        currentRoomId = nextRoomId;
        applyLocationStruct(nextLocation);

        if (!isUnderConstruction)
            previousRoomId.clear();
    }

    void Location::applyLocationStruct(const LocationStruct& locationStruct)
    {
        locationImage = locationStruct.locationImage;
        ownsLocationImage = locationStruct.ownsLocationImage;
        isUnderConstruction = locationStruct.isUnderConstruction;
        baseDescription = locationStruct.locationDescription;
        narrativeText = locationStruct.locationDescription;
        examineDetails = locationStruct.examineDetails;
        examineFlag = locationStruct.examineFlag;
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

        conversationMgr.onEnterRoom(currentRoomId, roomDatabase.getSpeakConfig(currentRoomId));

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

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
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

        const Rectangle dialog = getDialogBounds();
        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            dialog.y + yOffset,
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
            dialog.x,
            dialog.y,
            dialog.width - kScrollbarWidth,
            dialog.height
        };

        if (CheckCollisionPointRec(mousePos, textScrollArea))
            narrativeScrollY -= GetMouseWheelMove() * lineHeight * 3.0f;

        narrativeScrollY = std::max(0.0f, std::min(narrativeScrollY, maxScroll));

        if (narrativeContentHeight > visibleHeight && narrativeScrollY >= maxScroll - 1.0f)
            narrativeScrollY = maxScroll;
    }

    void Location::update()
    {
        updateInventoryLayout();
        updateActionAvailability();

        if (!inventoryMgr.isExaminingItem())
            handleNarrativeChoiceInput();

        buttonMgr.update();

        if (buttonMgr.consumeInventoryButtonClick())
        {
            if (inventoryMgr.isOpen())
                inventoryMgr.close();
            else
                inventoryMgr.open();

            updateInventoryLayout();
            updateActionAvailability();
        }

        if (inventoryMgr.isOpen())
        {
            inventoryMgr.update();

            if (buttonMgr.consumeBackwardButtonClick() && inventoryMgr.isExaminingItem())
            {
                inventoryMgr.returnToItemList();
                inventoryExamineScrollY = 0.0f;
                updateActionAvailability();
            }
            else if (buttonMgr.consumeExamineButtonClick() && inventoryMgr.canExamineSelectedItem())
            {
                inventoryMgr.examineSelectedItem();
                inventoryExamineScrollY = 0.0f;
                updateActionAvailability();
            }

            if (inventoryMgr.isExaminingItem())
                handleInventoryExamineScrollInput();
            else
                handleNarrativeScrollInput();

            updateActionAvailability();
            return;
        }

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

    void Location::handleInventoryExamineScrollInput()
    {
        const Rectangle dialog = getDialogBounds();
        const float visibleHeight = getNarrativeVisibleHeight();
        const float maxScroll = std::max(0.0f, inventoryExamineContentHeight - visibleHeight);
        const float lineHeight = getNarrativeLineHeight();

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            dialog.y + yOffset,
            kScrollbarWidth,
            visibleHeight
        };

        const float thumbHeight = (inventoryExamineContentHeight <= 0.0f)
            ? visibleHeight
            : std::max(24.0f, visibleHeight * (visibleHeight / inventoryExamineContentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (maxScroll > 0.0f
            ? (inventoryExamineScrollY / maxScroll) * thumbTravel
            : 0.0f);

        const Rectangle scrollThumb = {
            scrollTrack.x + 2.0f,
            thumbY,
            scrollTrack.width - 4.0f,
            thumbHeight
        };
        const Vector2 mousePos = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mousePos, scrollThumb))
            {
                inventoryExamineScrollbarDragging = true;
                inventoryExamineScrollbarDragOffsetY = mousePos.y - scrollThumb.y;
            }
            else if (CheckCollisionPointRec(mousePos, scrollTrack) && thumbTravel > 0.0f)
            {
                inventoryExamineScrollY =
                    ((mousePos.y - scrollTrack.y - thumbHeight * 0.5f) / thumbTravel) * maxScroll;
            }
        }

        if (inventoryExamineScrollbarDragging)
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && thumbTravel > 0.0f)
            {
                inventoryExamineScrollY =
                    ((mousePos.y - scrollTrack.y - inventoryExamineScrollbarDragOffsetY) / thumbTravel) * maxScroll;
            }
            else
            {
                inventoryExamineScrollbarDragging = false;
            }
        }

        const Rectangle textScrollArea = {
            dialog.x,
            dialog.y,
            dialog.width - kScrollbarWidth,
            dialog.height
        };

        if (CheckCollisionPointRec(mousePos, textScrollArea))
            inventoryExamineScrollY -= GetMouseWheelMove() * lineHeight * 3.0f;

        inventoryExamineScrollY = std::max(0.0f, std::min(inventoryExamineScrollY, maxScroll));
    }

    void Location::draw() const
    {
        ClearBackground(BLACK);

        drawMainImage();

        const Rectangle dialog = getDialogBounds();
        drawNotebookBackdrop(dialog);

        if (inventoryMgr.isOpen() && inventoryMgr.isExaminingItem())
        {
            drawInventoryExamineView();
            drawInventoryExamineScrollbar();
        }
        else
        {
            drawNarrativeText();
            drawNarrativeScrollbar();
        }

        if (inventoryMgr.isOpen())
            inventoryMgr.draw();

        buttonMgr.draw();
    }

    void Location::drawInventoryExamineScrollbar() const
    {
        const Rectangle dialog = getDialogBounds();
        const float visibleHeight = getNarrativeVisibleHeight();
        const float maxScroll = std::max(0.0f, inventoryExamineContentHeight - visibleHeight);

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            dialog.y + yOffset,
            kScrollbarWidth,
            visibleHeight
        };

        DrawRectangleRec(scrollTrack, kScrollTrack);

        if (maxScroll <= 0.0f)
            return;

        const float thumbHeight = std::max(24.0f, visibleHeight * (visibleHeight / inventoryExamineContentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (inventoryExamineScrollY / maxScroll) * thumbTravel;

        const Rectangle scrollThumb = {
            scrollTrack.x + 2.0f,
            thumbY,
            scrollTrack.width - 4.0f,
            thumbHeight
        };

        const bool thumbHovered = CheckCollisionPointRec(GetMousePosition(), scrollThumb);
        DrawRectangleRounded(scrollThumb, 0.4f, 6, thumbHovered ? kScrollThumbHover : kScrollThumb);
    }

    void Location::drawInventoryExamineView() const
    {
        const InventoryItem* item = inventoryMgr.getSelectedItem();
        if (!item)
            return;

        const Rectangle dialog = getDialogBounds();
        const Rectangle clipArea = {
            dialog.x,
            dialog.y,
            dialog.width - kScrollbarWidth,
            dialog.height
        };

        BeginScissorMode(
            (int)clipArea.x,
            (int)clipArea.y,
            (int)clipArea.width,
            (int)clipArea.height);

        float contentY = 0.0f;
        const float lineHeight = getNarrativeLineHeight();

        const char* header = "Examining:";
        DrawTextEx(
            boldFont,
            header,
            { dialog.x + xOffset, dialog.y + yOffset + contentY - inventoryExamineScrollY },
            fontSize,
            spacing,
            textColor);
        contentY += lineHeight * 1.2f;

        layoutWrappedParagraph(
            item->examineText.c_str(),
            descriptionFont,
            fontSize,
            contentY,
            true,
            inventoryExamineScrollY,
            textColor);

        inventoryExamineContentHeight = contentY;
        EndScissorMode();
    }

    void Location::drawNarrativeScrollbar() const
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        const Rectangle dialog = getDialogBounds();
        const float visibleHeight = getNarrativeVisibleHeight();
        const float maxScroll = std::max(0.0f, narrativeContentHeight - visibleHeight);

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            dialog.y + yOffset,
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

        rebuildNarrativeChoiceHitAreas();

        const Rectangle dialog = getDialogBounds();
        const Rectangle clipArea = {
            dialog.x,
            dialog.y,
            dialog.width - kScrollbarWidth,
            dialog.height
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

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            const Color lineColor = narrativeLineColor(line);
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
        const Rectangle dialog = getDialogBounds();

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

                    const float drawY = dialog.y + yOffset + textOffsetY - scrollY;
                    const bool visible = (textOffsetY + lineHeight > scrollY) &&
                                         (textOffsetY < scrollY + visibleHeight);

                    if (draw && visible && (codepoint != ' ') && (codepoint != '\t'))
                    {
                        DrawTextCodepoint(
                            font,
                            codepoint,
                            (Vector2){ dialog.x + xOffset + textOffsetX, drawY },
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
