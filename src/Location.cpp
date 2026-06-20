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

    Location::Location(
        const LocationStruct& locationStruct,
        Vector2 screenSize,
        SceneDatabase& sceneDatabase,
        AudioManager& audioManager,
        GameConfig& gameConfig,
        const std::string& sceneId,
        const std::string& configPath)
    : screenWidth((int)screenSize.x),
      screenHeight((int)screenSize.y),
      sceneDatabase(sceneDatabase),
      audioManager(audioManager),
      gameConfig(gameConfig),
      pauseMenu((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
      gameConfigPath(configPath),
      currentSceneId(sceneId),
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
      buttonMgr(buttonBox, locationStruct.uiFont, locationStruct.boldFont)
    {
        inventoryMgr.setFont(locationStruct.uiFont);
        takeMgr.setFont(locationStruct.uiFont);
        const std::string& assetRoot = sceneDatabase.getAssetRoot();
        const std::string fallbackRoot = (assetRoot == ".") ? ".." : ".";
        inventoryMgr.setAssetRoots(assetRoot, fallbackRoot);
        takeMgr.setAssetRoots(assetRoot, fallbackRoot);
        if (!inventoryMgr.ensureAssetsLoaded())
            TraceLog(LOG_WARNING, "Some inventory images failed to load at startup");
        trimNarrativeBuffer();
        conversationMgr.onEnterScene(currentSceneId, sceneDatabase.getSpeakConfig(currentSceneId));
        pauseMenu.setAudioManager(&audioManager);
        pauseMenu.setGameConfig(&gameConfig);
        pauseMenu.setConfigPath(gameConfigPath);
        pauseMenu.setOnDisplaySettingsChanged([this]() { applyDisplayConfig(); });
        pauseMenu.setOnInputSettingsChanged([this]() { applyInputConfig(); });
        applyInputConfig();
        updateInventoryLayout();
        updateActionAvailability();
    }

    void Location::relayoutForScreenSize(int width, int height)
    {
        screenWidth = width;
        screenHeight = height;
        textBox = {
            screenWidth / 2.0f,
            0.0f,
            screenWidth / 2.0f,
            screenHeight * 2.0f / 3.0f
        };
        buttonBox = {
            screenWidth / 2.0f,
            screenHeight * 2.0f / 3.0f,
            screenWidth / 2.0f,
            screenHeight / 3.0f
        };
        fullDialogHeight = screenHeight * 2.0f / 3.0f;
        buttonMgr.relayout(buttonBox);
        pauseMenu.setScreenSize(screenWidth, screenHeight);

        if (notebookPaperTextureReady)
        {
            UnloadTexture(notebookPaperTexture);
            notebookPaperTextureReady = false;
        }

        narrativeLayoutDirty = true;
        updateInventoryLayout();
    }

    void Location::applyDisplayConfig()
    {
        if (gameConfig.display.fullscreen)
        {
            if (!IsWindowFullscreen())
                ToggleFullscreen();

            relayoutForScreenSize(GetScreenWidth(), GetScreenHeight());
            return;
        }

        if (IsWindowFullscreen())
            ToggleFullscreen();

        SetWindowSize(gameConfig.display.width, gameConfig.display.height);
        relayoutForScreenSize(gameConfig.display.width, gameConfig.display.height);
    }

    void Location::applyInputConfig()
    {
        buttonMgr.setClickHoldDuration(gameConfig.input.clickHoldSeconds);
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
            const float fontSize = 42.0f;
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

    bool Location::isSidePanelOpen() const
    {
        return inventoryMgr.isOpen() || takeMgr.isOpen();
    }

    std::string Location::takenItemKey(const std::string& itemId) const
    {
        return currentSceneId + ":" + itemId;
    }

    std::vector<TakeableItemDef> Location::getAvailableTakeables() const
    {
        std::vector<TakeableItemDef> available;
        const std::vector<TakeableItemDef>& takeables = sceneDatabase.getTakeables(currentSceneId);

        for (const TakeableItemDef& item : takeables)
        {
            if (takenItemKeys.count(takenItemKey(item.id)) > 0)
                continue;

            if (item.requiresExamine && !hasExaminedScene(currentSceneId))
                continue;

            available.push_back(item);
        }

        return available;
    }

    bool Location::canTakeInCurrentScene() const
    {
        return !getAvailableTakeables().empty();
    }

    void Location::addTakenItemToInventory(const TakeableItemDef& taken)
    {
        if (inventoryMgr.hasItem(taken.id))
            return;

        InventoryItem item;
        item.id = taken.id;
        item.name = taken.name;
        item.iconPath = taken.iconPath;
        item.examineImagePath = taken.examineImagePath;
        item.examineText = taken.examineText;
        inventoryMgr.addItem(item);
    }

    void Location::refreshTakeItems()
    {
        takeMgr.setAvailableItems(getAvailableTakeables());
    }

    void Location::processPendingTakes()
    {
        if (takeMgr.consumePendingTakeAll())
        {
            const std::vector<TakeableItemDef> takenItems = takeMgr.takeAll();
            for (const TakeableItemDef& taken : takenItems)
            {
                takenItemKeys.insert(takenItemKey(taken.id));
                addTakenItemToInventory(taken);
            }
            updateActionAvailability();
            return;
        }

        const std::string takeId = takeMgr.consumePendingTakeId();
        if (takeId.empty())
            return;

        TakeableItemDef taken;
        if (!takeMgr.tryTakeItem(takeId, taken))
            return;

        takenItemKeys.insert(takenItemKey(taken.id));
        addTakenItemToInventory(taken);
        updateActionAvailability();
    }

    Rectangle Location::getDialogBounds() const
    {
        const float height = isSidePanelOpen() ? fullDialogHeight * 0.5f : fullDialogHeight;
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
        if (inventoryMgr.isOpen() || takeMgr.isOpen())
        {
            const Rectangle panelBounds = getInventoryPanelBounds();
            if (inventoryMgr.isOpen())
                inventoryMgr.setPanelBounds(panelBounds);
            if (takeMgr.isOpen())
                takeMgr.setPanelBounds(panelBounds);
        }
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
            || (conversationMgr.isAwaitingChoice() && isDialogChoiceLine(line))
            || committedPlayerDialogLines.count(line) > 0;
    }

    bool Location::hasExaminedScene(const std::string& sceneId) const
    {
        return examinedSceneIds.count(sceneId) > 0;
    }

    bool Location::canUseInCurrentScene() const
    {
        if (useDetails.empty() || !hasExaminedScene(currentSceneId))
            return false;

        if (useRepeatStatus)
            return !hasUsedInCurrentScene;

        return usedSceneIds.count(currentSceneId) == 0;
    }

    bool Location::isDialogChoiceLine(const std::string& line) const
    {
        return line.size() > 2 && line.compare(0, 2, "> ") == 0;
    }

    Color Location::narrativeLineColor(const std::string& line) const
    {
        if (!conversationMgr.isAwaitingChoice() || !isDialogChoiceLine(line))
            return textColor;

        for (const ConversationChoiceDef& choice : conversationMgr.getPendingChoices())
        {
            if (line == choice.label)
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
        examinedSceneIds.insert(currentSceneId);
        updateActionAvailability();
    }

    void Location::appendSpeakDetails()
    {
        if (speakDetails.empty() || hasSpokenInCurrentScene)
            return;

        appendNarrativeSection("Speaking:", speakDetails);
        hasSpokenInCurrentScene = true;
        updateActionAvailability();
    }

    void Location::appendChoiceLinesToNarrative(const std::vector<ConversationChoiceDef>& choices)
    {
        for (const ConversationChoiceDef& choice : choices)
        {
            narrativeText += "\n";
            narrativeText += choice.label;
        }

        trimNarrativeBuffer();
        narrativeLayoutDirty = true;
        updateActionAvailability();

        if (!choices.empty())
            scrollNarrativeToLine(choices.front().label, false);
    }

    void Location::stripDialogChoiceLinesFromNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText)
    {
        if (choices.empty())
            return;

        std::vector<std::string> lines;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            bool isChoiceLine = false;
            for (const ConversationChoiceDef& choice : choices)
            {
                if (line == choice.label)
                {
                    isChoiceLine = true;
                    break;
                }
            }

            if (isChoiceLine && line == keepLineText)
            {
                lines.push_back(line);
                continue;
            }

            if (!isChoiceLine)
                lines.push_back(line);
        }

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

    void Location::applyStatusEffects(const std::vector<StatusEffect>& effects)
    {
        bool statsChanged = false;

        for (const StatusEffect& effect : effects)
        {
            if (!effect.hasDelta())
                continue;

            if (tryApplyStatusEffect(effect, effect.repeat))
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
            appendChoiceLinesToNarrative(conversationMgr.getPendingChoices());
            return;
        }

        applyStatusEffects(result.statusEffects);
    }

    void Location::resolveDialogChoice(const std::string& choiceId)
    {
        if (!conversationMgr.isAwaitingChoice())
            return;

        const std::vector<ConversationChoiceDef> choicesToStrip = conversationMgr.getPendingChoices();

        std::string selectedLineText;
        for (const ConversationChoiceDef& choice : choicesToStrip)
        {
            if (choice.id == choiceId)
            {
                selectedLineText = choice.label;
                break;
            }
        }

        SpeakResult result = conversationMgr.resolveChoice(choiceId);
        if (result.action == SpeakResult::Action::None && result.narrative.empty())
        {
            const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(currentSceneId);
            result = conversationMgr.resolveChoiceFromConfig(speakConfig, choiceId);
        }

        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            return;

        const std::string responseText = result.narrative;
        const std::vector<StatusEffect> effects = result.statusEffects;

        narrativeChoiceHitAreas.clear();

        if (!selectedLineText.empty())
            committedPlayerDialogLines.insert(selectedLineText);

        stripDialogChoiceLinesFromNarrative(choicesToStrip, selectedLineText);

        if (!responseText.empty())
        {
            narrativeText += "\n\n";
            narrativeText += responseText;
            trimNarrativeBuffer();
            narrativeLayoutDirty = true;
        }

        applyStatusEffects(effects);

        if (!responseText.empty())
        {
            rebuildNarrativeLayout();
            std::istringstream responseStream(responseText);
            std::string firstLine;
            if (std::getline(responseStream, firstLine) && !firstLine.empty())
                scrollNarrativeToLine(firstLine, true);
        }

        updateActionAvailability();
    }

    void Location::resolveCombatEncounter(const std::string& encounterId)
    {
        if (!conversationMgr.isAwaitingChoice() || !conversationMgr.isCombatAttackAllowed())
            return;

        if (conversationMgr.getCombatEncounterId() != encounterId)
            return;

        const std::vector<ConversationChoiceDef> choicesToStrip = conversationMgr.getPendingChoices();
        narrativeChoiceHitAreas.clear();
        stripDialogChoiceLinesFromNarrative(choicesToStrip);

        std::string responseText;
        StatusEffect effect;

        if (encounterId == "drunk_patron")
        {
            if (resolve > 65.0f)
            {
                responseText =
                    "You do not think. You move.\n\n"
                    "The drunk man's shove becomes the opening he did not intend. You catch his wrist on the way past, "
                    "step inside his reach, and drive your shoulder into his sternum hard enough to lift him off the "
                    "foot rail. His breath leaves in a wet bark. The tumbler flies, spinning amber through the lamp "
                    "smoke. He hits the sawdust on his back with a thud that stops the piano mid-phrase.\n\n"
                    "For one heartbeat the saloon is silent. Then the room detonates into laughter, applause, and "
                    "the hungry whistle of men who have paid good money to see violence that is not their own. "
                    "Someone shouts for another round. A woman on the balcony leans over the rail and fans herself "
                    "with deliberate slowness. The faro dealer does not look up, but his mouth curves.\n\n"
                    "The drunk man wheezes and tries to rise. You put a boot lightly on his chest and hold him there "
                    "long enough for the point to settle. Not cruel. Measured. Then you step back and let the bartender "
                    "take over.\n\n"
                    "\"All right,\" the bartender says, not angry, not impressed. Simply in charge. \"Show's over. "
                    "Drinks still cost money.\"";
                effect.key = "saloon_interior:drunk_patron_win";
                effect.health = -5.0f;
                effect.energy = -10.0f;
                effect.lucidity = 10.0f;
                effect.charisma = 10.0f;
            }
            else
            {
                responseText =
                    "You swing before you think and that is the first mistake.\n\n"
                    "The drunk man is faster than a man that soaked has any right to be. He slips your grab, "
                    "catches you across the jaw with a ringed knuckle, and drives you sideways into the bar edge. "
                    "Your ribs meet polished pine. The room blurs into lamp halos and laughing mouths. A second shove "
                    "puts you on the floor, cheek in sawdust, the taste of whiskey and blood mixing under your tongue.\n\n"
                    "The saloon does not stop. It watches. Delighted. A miner hoots. The piano player finds the same "
                    "broken phrase and plays it louder, as though the music is part of the joke. Someone calls you "
                    "green. Someone else calls for odds on whether you will stand up again.\n\n"
                    "Boots cross the sawdust. The bartender hauls you up by the collar with practiced efficiency, "
                    "props you against the bar long enough to see straight, and shoves a wet rag into your hand.\n\n"
                    "\"Out of my floor,\" he tells the drunk man. \"Go sleep it off before I throw you off the ridge.\"\n\n"
                    "The drunk man laughs, spits sawdust, and stumbles toward the door with the triumphant swagger of "
                    "a man who won nothing but the room's attention. The bartender does not apologize for you. "
                    "He does not need to. The bar has already filed the scene under entertainment and moved on.";
                effect.key = "saloon_interior:drunk_patron_lose";
                effect.health = -30.0f;
                effect.energy = -20.0f;
                effect.charisma = -5.0f;
            }
        }
        else
        {
            return;
        }

        conversationMgr.clearPendingEncounter();

        if (!responseText.empty())
        {
            narrativeText += "\n\n";
            narrativeText += responseText;
            trimNarrativeBuffer();
            narrativeLayoutDirty = true;
        }

        applyStatusEffects({ effect });

        if (!responseText.empty())
        {
            rebuildNarrativeLayout();
            std::istringstream responseStream(responseText);
            std::string firstLine;
            if (std::getline(responseStream, firstLine) && !firstLine.empty())
                scrollNarrativeToLine(firstLine, true);
        }

        updateActionAvailability();
    }

    void Location::handleAttack()
    {
        if (!conversationMgr.isCombatAttackAllowed())
            return;

        resolveCombatEncounter(conversationMgr.getCombatEncounterId());
    }

    void Location::handleSpeak()
    {
        const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(currentSceneId);
        if (speakConfig.hasPhases())
        {
            SpeakResult result = conversationMgr.handleSpeak(
                currentSceneId,
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
        std::string startSceneId;
        if (!sceneDatabase.loadStartScene(cabinLocation, startSceneId))
        {
            TraceLog(LOG_ERROR, "Failed to restart at cabin after lucidity collapse");
            return;
        }

        if (ownsLocationImage && locationImage.id != 0)
            UnloadTexture(locationImage);
        previousSceneId.clear();
        currentSceneId = startSceneId;
        examinedSceneIds.clear();
        usedSceneIds.clear();
        takenItemKeys.clear();
        takeMgr.close();
        inventoryMgr.close();
        applyLocationStruct(cabinLocation);

        narrativeText += "\n\n";
        narrativeText += kWakeOnFloorPrefix;
        narrativeText += "\n\n";
        narrativeText += baseDescription;
        health = 90.0f;
        energy = 20.0f;
        resolve = 50.0f;
        lucidity = 30.0f;
        charisma = 50.0f;
        walletCash = 20.0f;
        narrativeScrollY = 0.0f;
        narrativeLayoutDirty = true;
        trimNarrativeBuffer();
        updateActionAvailability();
    }

    void Location::handleNarrativeChoiceInput()
    {
        if (!conversationMgr.isAwaitingChoice())
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
                return;
            }
        }
    }

    bool Location::tryApplyStatusEffect(const StatusEffect& effect, bool allowRepeat)
    {
        if (!effect.hasDelta())
            return false;

        if (!allowRepeat && !effect.key.empty() && consumedStatusActions.count(effect.key) > 0)
            return false;

        health = std::min(100.0f, std::max(0.0f, health + effect.health));
        energy = std::min(100.0f, std::max(0.0f, energy + effect.energy));
        resolve = std::min(100.0f, std::max(0.0f, resolve + effect.resolve));
        lucidity = std::min(100.0f, std::max(0.0f, lucidity + effect.lucidity));
        charisma = std::min(100.0f, std::max(0.0f, charisma + effect.charisma));
        walletCash = std::max(0.0f, walletCash + effect.money);

        if (!allowRepeat && !effect.key.empty())
            consumedStatusActions.insert(effect.key);

        return true;
    }

    void Location::appendUseDetails()
    {
        if (!canUseInCurrentScene())
            return;

        appendNarrativeSection("Using:", useDetails);
        StatusEffect useEffect;
        useEffect.key = currentSceneId + ":use";
        useEffect.health = useHealthDelta;
        useEffect.energy = useEnergyDelta;
        tryApplyStatusEffect(useEffect, useRepeatStatus);
        hasUsedInCurrentScene = true;
        if (!useRepeatStatus)
            usedSceneIds.insert(currentSceneId);
        updateActionAvailability();
    }

    void Location::updateActionAvailability()
    {
        MovementStruct movement{};
        ActionStruct actions{};

        if (takeMgr.isOpen())
        {
            movement.up = false;
            movement.down = false;
            movement.forward = false;
            movement.left = false;
            movement.right = false;
            movement.backward = false;
            actions.take = true;
        }
        else if (inventoryMgr.isOpen())
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
        else if (conversationMgr.isAwaitingChoice())
        {
            movement.up = false;
            movement.down = false;
            movement.forward = false;
            movement.backward = false;
            movement.left = false;
            movement.right = false;
            actions.hit = conversationMgr.isCombatAttackAllowed();
        }
        else if (isUnderConstruction)
        {
            movement.backward = !previousSceneId.empty();
        }
        else
        {
            movement.up = up;
            movement.down = down;
            movement.forward = forward;
            movement.backward = backward;
            movement.left = left;
            movement.right = right;

            if (currentSceneId == "saloon_interior" && !hasExaminedScene(currentSceneId))
            {
                movement.up = false;
                movement.right = false;
            }

            actions = baseActionFilter;
            const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(currentSceneId);
            if (speakConfig.hasPhases())
                actions.speak = conversationMgr.canSpeak(
                    speakConfig,
                    baseActionFilter.speak,
                    storyFlags);
            else if (!speakDetails.empty())
                actions.speak = !hasSpokenInCurrentScene;
            else
                actions.speak = false;

            actions.use = canUseInCurrentScene();
            actions.take = canTakeInCurrentScene();
        }

        buttonMgr.setAvailability(movement, actions);
        buttonMgr.setStatus(health, energy, resolve, lucidity, charisma);
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

        if (!conversationMgr.isAwaitingChoice() || conversationMgr.getPendingChoices().empty())
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
                for (const ConversationChoiceDef& choice : conversationMgr.getPendingChoices())
                {
                    if (line != choice.label)
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
            if (direction != "backward" || previousSceneId.empty())
                return;

            LocationStruct previousLocation;
            if (!sceneDatabase.loadScene(previousSceneId, previousLocation))
            {
                TraceLog(LOG_ERROR, "Failed to load previous scene '%s'", previousSceneId.c_str());
                return;
            }

            if (ownsLocationImage && locationImage.id != 0)
                UnloadTexture(locationImage);

            const std::string exitSceneId = currentSceneId;
            const std::string returnSceneId = previousSceneId;
            audioManager.onRoomExit(sceneDatabase.getSceneAudio(currentSceneId), returnSceneId);
            previousSceneId.clear();
            currentSceneId = returnSceneId;
            applyLocationStruct(previousLocation, exitSceneId);
            return;
        }

        const std::string nextSceneId = sceneDatabase.getExitSceneId(currentSceneId, direction);
        if (nextSceneId.empty())
        {
            TraceLog(LOG_WARNING, "No exit '%s' from scene '%s'", direction.c_str(), currentSceneId.c_str());
            return;
        }

        LocationStruct nextLocation;
        if (!sceneDatabase.loadScene(nextSceneId, nextLocation))
        {
            TraceLog(LOG_ERROR, "Failed to load scene '%s'", nextSceneId.c_str());
            return;
        }

        if (ownsLocationImage && locationImage.id != 0)
            UnloadTexture(locationImage);

        const std::string fromSceneId = currentSceneId;
        audioManager.onRoomExit(sceneDatabase.getSceneAudio(currentSceneId), nextSceneId);
        previousSceneId = fromSceneId;
        currentSceneId = nextSceneId;
        applyLocationStruct(nextLocation, fromSceneId);

        if (!isUnderConstruction)
            previousSceneId.clear();
    }

    void Location::applyLocationStruct(const LocationStruct& locationStruct, const std::string& fromRoom)
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
        hasSpokenInCurrentScene = false;
        hasUsedInCurrentScene = false;
        committedPlayerDialogLines.clear();
        narrativeChoiceHitAreas.clear();

        conversationMgr.onEnterScene(currentSceneId, sceneDatabase.getSpeakConfig(currentSceneId));
        audioManager.onRoomEnter(sceneDatabase.getSceneAudio(currentSceneId), fromRoom);

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

    SavedGameState Location::captureSaveState() const
    {
        SavedGameState state;
        state.sceneId = currentSceneId;
        state.previousSceneId = previousSceneId;
        state.narrativeText = narrativeText;
        state.health = health;
        state.energy = energy;
        state.resolve = resolve;
        state.lucidity = lucidity;
        state.charisma = charisma;
        state.walletCash = walletCash;
        state.hasSpokenInCurrentScene = hasSpokenInCurrentScene;
        state.hasUsedInCurrentScene = hasUsedInCurrentScene;
        state.examinedSceneIds = examinedSceneIds;
        state.usedSceneIds = usedSceneIds;
        state.takenItemKeys = takenItemKeys;
        state.storyFlags = storyFlags;
        state.consumedStatusActions = consumedStatusActions;
        state.committedPlayerDialogLines = committedPlayerDialogLines;
        state.inventoryItems = inventoryMgr.exportItemSnapshots();
        conversationMgr.exportPersistState(state.conversation);
        return state;
    }

    bool Location::applySaveState(const SavedGameState& state)
    {
        LocationStruct locationStruct;
        if (!sceneDatabase.loadScene(state.sceneId, locationStruct))
            return false;

        if (ownsLocationImage && locationImage.id != 0)
            UnloadTexture(locationImage);

        const std::string fromSceneId = currentSceneId;
        audioManager.onRoomExit(sceneDatabase.getSceneAudio(currentSceneId), state.sceneId);

        currentSceneId = state.sceneId;
        previousSceneId = state.previousSceneId;
        applyLocationStruct(locationStruct, fromSceneId);

        narrativeText = state.narrativeText;
        health = state.health;
        energy = state.energy;
        resolve = state.resolve;
        lucidity = state.lucidity;
        charisma = state.charisma;
        walletCash = state.walletCash;
        hasSpokenInCurrentScene = state.hasSpokenInCurrentScene;
        hasUsedInCurrentScene = state.hasUsedInCurrentScene;
        examinedSceneIds = state.examinedSceneIds;
        usedSceneIds = state.usedSceneIds;
        takenItemKeys = state.takenItemKeys;
        storyFlags = state.storyFlags;
        consumedStatusActions = state.consumedStatusActions;
        committedPlayerDialogLines = state.committedPlayerDialogLines;
        narrativeScrollY = 0.0f;
        narrativeLayoutDirty = true;

        inventoryMgr.restoreFromSnapshots(state.inventoryItems);
        conversationMgr.importPersistState(state.conversation);
        takeMgr.close();
        narrativeChoiceHitAreas.clear();

        audioManager.onRoomEnter(sceneDatabase.getSceneAudio(currentSceneId), fromSceneId);
        trimNarrativeBuffer();
        updateInventoryLayout();
        updateActionAvailability();
        return true;
    }

    bool Location::saveGameToDisk()
    {
        return writeSaveFile(defaultSavePath(), captureSaveState());
    }

    bool Location::loadGameFromDisk()
    {
        SavedGameState state;
        if (!readSaveFile(defaultSavePath(), state))
            return false;

        return applySaveState(state);
    }

    void Location::handlePauseMenuInput()
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            if (pauseMenu.isOpen())
            {
                if (pauseMenu.isConfigPanel())
                    pauseMenu.showMainPanel();
                else
                {
                    pauseMenu.closeMenu();
                    audioManager.setGameplayPaused(false);
                }
            }
            else
            {
                pauseMenu.openMenu();
                audioManager.setGameplayPaused(true);
            }
        }

        pauseMenu.update(GetFrameTime());

        if (pauseMenu.consumeSaveRequest())
        {
            if (saveGameToDisk())
                pauseMenu.setStatusMessage("Game saved.");
            else
                pauseMenu.setStatusMessage("Save failed.");
        }

        if (pauseMenu.consumeLoadRequest())
        {
            if (loadGameFromDisk())
                pauseMenu.setStatusMessage("Game loaded.");
            else if (!saveFileExists(defaultSavePath()))
                pauseMenu.setStatusMessage("No save file found.");
            else
                pauseMenu.setStatusMessage("Load failed.");
        }

        if (pauseMenu.consumeQuitRequest())
            quitRequested = true;
    }

    void Location::update()
    {
        if (!initialFrameComplete)
        {
            initialFrameComplete = true;
        }
        else if (deferInitialRoomAudio)
        {
            audioManager.onRoomEnter(sceneDatabase.getSceneAudio(currentSceneId));
            deferInitialRoomAudio = false;
        }

        audioManager.update(GetFrameTime());
        handlePauseMenuInput();

        if (pauseMenu.isOpen())
            return;

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
            {
                if (takeMgr.isOpen())
                    takeMgr.close();
                inventoryMgr.open();
            }

            updateInventoryLayout();
            updateActionAvailability();
        }

        if (buttonMgr.consumeTakeButtonClick())
        {
            if (takeMgr.isOpen())
                takeMgr.close();
            else
            {
                if (inventoryMgr.isOpen())
                    inventoryMgr.close();
                refreshTakeItems();
                takeMgr.open();
            }

            updateInventoryLayout();
            updateActionAvailability();
        }

        if (takeMgr.isOpen())
        {
            takeMgr.update();
            processPendingTakes();
            handleNarrativeScrollInput();
            updateActionAvailability();
            return;
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

        if (buttonMgr.consumeHitButtonClick())
            handleAttack();

        if (!conversationMgr.isAwaitingChoice())
        {
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
        }

        if (!conversationMgr.isAwaitingChoice() && buttonMgr.consumeExamineButtonClick())
        {
            appendExamineDetails();
            scrollNarrativeToHeader("Examining:");
        }

        if (!conversationMgr.isAwaitingChoice() && buttonMgr.consumeSpeakButtonClick())
        {
            handleSpeak();
            scrollNarrativeToHeader("Speaking:");
        }

        if (!conversationMgr.isAwaitingChoice() && buttonMgr.consumeUseButtonClick())
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
        else if (takeMgr.isOpen())
            takeMgr.draw();

        buttonMgr.draw();
        pauseMenu.draw();
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
