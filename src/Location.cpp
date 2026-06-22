#include <Location.h>
#include <ImageCompression.h>
#include <RaylibCompat.h>
#include <raylib.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace testgame
{

const float Location::kScrollbarWidth = 16.0f;

namespace
{
    const float kDialogHeightShareWhenSidePanelOpen = 2.0f / 3.0f;
    const float kSidePanelHeightShare = 1.0f / 3.0f;
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
    const char kNarrativeSketchPrefix[] = "@sketch:";
    const Color kNotebookHeader = {78, 54, 34, 255};
    const Color kNotebookNavEnabled = {78, 54, 34, 255};
    const Color kNotebookNavDisabled = {148, 132, 112, 255};
    const Color kQuestComplete = {52, 92, 58, 255};
    const Color kQuestFailed = {108, 72, 72, 255};

    Rectangle getNotebookHeaderBand(const Rectangle& bounds)
    {
        return {
            bounds.x + 58.0f,
            bounds.y,
            bounds.width - 58.0f - 16.0f,
            46.0f
        };
    }

    void drawCenteredUnderlinedHeader(
        Font font,
        const char* title,
        const Rectangle& band,
        float fontSize,
        Color color)
    {
        const Vector2 textSize = MeasureTextEx(font, title, fontSize, 1.0f);
        const float textX = band.x + (band.width - textSize.x) / 2.0f;
        const float textY = band.y + (band.height - textSize.y) / 2.0f - 2.0f;

        DrawTextEx(font, title, { textX, textY }, fontSize, 1.0f, color);
        DrawLineEx(
            { textX, textY + textSize.y + 2.0f },
            { textX + textSize.x, textY + textSize.y + 2.0f },
            1.5f,
            color);
    }

    void drawNotebookArrowButton(
        Font font,
        const char* label,
        Rectangle bounds,
        bool enabled,
        bool hovered)
    {
        const float fontSize = bounds.height - 4.0f;
        const Vector2 textSize = MeasureTextEx(font, label, fontSize, 1.0f);
        const Vector2 textPos = {
            bounds.x + (bounds.width - textSize.x) / 2.0f,
            bounds.y + (bounds.height - textSize.y) / 2.0f
        };

        Color color = enabled ? kNotebookNavEnabled : kNotebookNavDisabled;
        if (enabled && hovered)
            color = kChoiceHover;

        DrawTextEx(font, label, textPos, fontSize, 1.0f, color);
    }

    const char* kWakeOnFloorPrefix =
        "You come back to yourself on the floor of the cabin, cheek pressed to the Persian rug, "
        "the fireplace crackling somewhere near your shoulder. Your mouth tastes of ash and bourbon. "
        "How you got here from the saloon is a blank page. How you got here at all is no clearer than before.";
}

    Location::Location(
        const LocationStruct& locationStruct,
        Vector2 screenSize,
        SceneDatabase& sceneDatabase,
        const ItemDatabase& itemDatabase,
        const ItemCombinationDatabase& itemCombinationDatabase,
        const MilestoneDatabase& milestoneDatabase,
        AudioManager& audioManager,
        GameConfig& gameConfig,
        const std::string& sceneId,
        const std::string& configPath)
    : screenWidth((int)screenSize.x),
      screenHeight((int)screenSize.y),
      sceneDatabase(sceneDatabase),
      itemDatabase(itemDatabase),
      itemCombinationDatabase(itemCombinationDatabase),
      milestoneMgr(milestoneDatabase),
      audioManager(audioManager),
      gameConfig(gameConfig),
      pauseMenu((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
      saveLoadMenu((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
      dropConfirmMgr((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
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
      useRequiresExamine(locationStruct.useRequiresExamine),
      useExit(locationStruct.useExit),
      descriptionFont(locationStruct.descriptionFont),
      boldFont(locationStruct.boldFont),
      baseActionFilter(locationStruct.actionFilter),
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
        interactionMgr.setFont(locationStruct.uiFont);
        const std::string& assetRoot = sceneDatabase.getAssetRoot();
        const std::string fallbackRoot = (assetRoot == ".") ? ".." : ".";
        inventoryMgr.setAssetRoots(assetRoot, fallbackRoot);
        inventoryMgr.setItemDatabase(&itemDatabase);
        inventoryMgr.setItemCombinationDatabase(&itemCombinationDatabase);
        takeMgr.setAssetRoots(assetRoot, fallbackRoot);
        if (!inventoryMgr.ensureIconAssetsLoaded())
            TraceLog(LOG_WARNING, "Some inventory icons failed to load at startup");
        trimNarrativeBuffer();
        conversationMgr.onEnterScene(currentSceneId, sceneDatabase.getSpeakConfig(currentSceneId));
        evaluateMilestones();
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
        saveLoadMenu.setScreenSize(screenWidth, screenHeight);
        dropConfirmMgr.setScreenSize(screenWidth, screenHeight);

        if (notebookPaperTextureReady)
        {
            UnloadTexture(notebookPaperTexture);
            notebookPaperTextureReady = false;
        }

        narrativeLayoutDirty = true;
        inventoryMgr.reloadItemIconsIfNeeded();
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
        const float firstLineY = bounds.y + notebookHeaderReserve + lineStep * 0.15f;
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

    void Location::drawNotebookHeader(const Rectangle& bounds) const
    {
        const Rectangle headerBand = getNotebookHeaderBand(bounds);
        const char* title = notebookPage == NotebookPage::CaseNotes ? "CASE NOTES" : "TO DO";
        drawCenteredUnderlinedHeader(
            boldFont,
            title,
            headerBand,
            notebookHeaderFontSize,
            kNotebookHeader);
    }

    void Location::drawNotebookNavButtons(const Rectangle& bounds) const
    {
        const bool prevEnabled = notebookPage != NotebookPage::CaseNotes;
        const bool nextEnabled = notebookPage != NotebookPage::Todo;

        const float arrowFontSize = notebookHeaderFontSize;
        const Vector2 arrowSize = MeasureTextEx(boldFont, "<", arrowFontSize, 1.0f);
        const float buttonWidth = arrowSize.x + 6.0f;
        const float buttonHeight = arrowSize.y + 4.0f;
        const float gap = 4.0f;
        const float topPad = 10.0f;
        const float rightPad = 8.0f;

        const float buttonsRight = bounds.x + bounds.width - kScrollbarWidth - rightPad;
        const Rectangle nextBounds = {
            buttonsRight - buttonWidth,
            bounds.y + topPad,
            buttonWidth,
            buttonHeight
        };
        const Rectangle prevBounds = {
            nextBounds.x - gap - buttonWidth,
            bounds.y + topPad,
            buttonWidth,
            buttonHeight
        };

        const Vector2 mousePos = GetMousePosition();
        const bool prevHovered = prevEnabled && CheckCollisionPointRec(mousePos, prevBounds);
        const bool nextHovered = nextEnabled && CheckCollisionPointRec(mousePos, nextBounds);

        drawNotebookArrowButton(boldFont, "<", prevBounds, prevEnabled, prevHovered);
        drawNotebookArrowButton(boldFont, ">", nextBounds, nextEnabled, nextHovered);
    }

    bool Location::canUseNotebookNav() const
    {
        if (pauseMenu.isOpen() || dropConfirmMgr.isOpen())
            return false;
        if (takeMgr.isOpen() || interactionMgr.isOpen())
            return false;
        if (inventoryMgr.isOpen() && inventoryMgr.isExaminingItem())
            return false;
        return true;
    }

    void Location::handleNotebookNavInput()
    {
        if (!canUseNotebookNav())
            return;

        const Rectangle dialog = getDialogBounds();
        const bool prevEnabled = notebookPage != NotebookPage::CaseNotes;
        const bool nextEnabled = notebookPage != NotebookPage::Todo;

        const float arrowFontSize = notebookHeaderFontSize;
        const Vector2 arrowSize = MeasureTextEx(boldFont, "<", arrowFontSize, 1.0f);
        const float buttonWidth = arrowSize.x + 6.0f;
        const float buttonHeight = arrowSize.y + 4.0f;
        const float gap = 4.0f;
        const float topPad = 10.0f;
        const float rightPad = 8.0f;

        const float buttonsRight = dialog.x + dialog.width - kScrollbarWidth - rightPad;
        const Rectangle nextBounds = {
            buttonsRight - buttonWidth,
            dialog.y + topPad,
            buttonWidth,
            buttonHeight
        };
        const Rectangle prevBounds = {
            nextBounds.x - gap - buttonWidth,
            dialog.y + topPad,
            buttonWidth,
            buttonHeight
        };

        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            return;

        const Vector2 mousePos = GetMousePosition();
        if (prevEnabled && CheckCollisionPointRec(mousePos, prevBounds))
        {
            notebookPage = NotebookPage::CaseNotes;
            todoScrollY = 0.0f;
        }
        else if (nextEnabled && CheckCollisionPointRec(mousePos, nextBounds))
        {
            notebookPage = NotebookPage::Todo;
            todoScrollY = 0.0f;
        }
    }

    void Location::drawQuestTodoPage() const
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle clipArea = getNotebookContentBounds();

        BeginScissorMode(
            (int)clipArea.x,
            (int)clipArea.y,
            (int)clipArea.width,
            (int)clipArea.height);

        const float lineHeight = getNarrativeLineHeight();
        float contentY = 0.0f;
        const std::vector<const MilestoneDef*> quests = milestoneMgr.getVisibleQuests();

        const Rectangle content = getNotebookContentBounds();

        if (quests.empty())
        {
            const char* emptyText = "No quests on record yet.";
            DrawTextEx(
                descriptionFont,
                emptyText,
                { dialog.x + xOffset, content.y + contentY - todoScrollY },
                fontSize,
                spacing,
                Fade(textColor, 0.72f));
            contentY += lineHeight * 1.5f;
        }
        else
        {
            for (const MilestoneDef* quest : quests)
            {
                if (!quest)
                    continue;

                const MilestoneStatus status = milestoneMgr.getStatus(quest->id);
                std::string questBody;
                if (status == MilestoneStatus::Complete && !quest->completeSummary.empty())
                    questBody = quest->completeSummary;
                else if (!quest->title.empty())
                    questBody = quest->title;
                else
                    questBody = quest->summary;

                std::string displayLine;
                Color lineColor = textColor;
                if (status == MilestoneStatus::Complete)
                {
                    displayLine = std::string("+ ") + questBody;
                    lineColor = kQuestComplete;
                }
                else
                {
                    displayLine = std::string("- ") + questBody;
                    if (status == MilestoneStatus::Failed)
                        lineColor = kQuestFailed;
                }

                const float blockStartY = contentY;
                layoutWrappedParagraph(
                    displayLine.c_str(),
                    status == MilestoneStatus::Complete ? boldFont : descriptionFont,
                    fontSize,
                    contentY,
                    true,
                    todoScrollY,
                    lineColor);

                if (status == MilestoneStatus::Failed)
                {
                    const float drawY = content.y + blockStartY - todoScrollY;
                    const float blockHeight = std::max(lineHeight, contentY - blockStartY);
                    const float strikeY = drawY + blockHeight * 0.55f;
                    DrawLineEx(
                        { dialog.x + xOffset, strikeY },
                        { dialog.x + xOffset + getNarrativeWrapWidth(), strikeY },
                        1.5f,
                        kQuestFailed);
                }

                contentY += lineHeight * 0.35f;
            }
        }

        todoContentHeight = contentY;
        EndScissorMode();
    }

    void Location::drawTodoScrollbar() const
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const float maxScroll = std::max(0.0f, todoContentHeight - visibleHeight);

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            visibleHeight
        };

        DrawRectangleRec(scrollTrack, kScrollTrack);

        if (maxScroll <= 0.0f)
            return;

        const float thumbHeight = std::max(24.0f, visibleHeight * (visibleHeight / todoContentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (todoScrollY / maxScroll) * thumbTravel;

        const Rectangle scrollThumb = {
            scrollTrack.x + 2.0f,
            thumbY,
            scrollTrack.width - 4.0f,
            thumbHeight
        };

        const bool thumbHovered = CheckCollisionPointRec(GetMousePosition(), scrollThumb);
        DrawRectangleRounded(scrollThumb, 0.4f, 6, thumbHovered ? kScrollThumbHover : kScrollThumb);
    }

    void Location::handleTodoScrollInput()
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const float maxScroll = std::max(0.0f, todoContentHeight - visibleHeight);
        const float lineHeight = getNarrativeLineHeight();

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            visibleHeight
        };

        const float thumbHeight = (todoContentHeight <= 0.0f)
            ? visibleHeight
            : std::max(24.0f, visibleHeight * (visibleHeight / todoContentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (maxScroll > 0.0f
            ? (todoScrollY / maxScroll) * thumbTravel
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
                todoScrollbarDragging = true;
                todoScrollbarDragOffsetY = mousePos.y - scrollThumb.y;
            }
            else if (CheckCollisionPointRec(mousePos, scrollTrack) && thumbTravel > 0.0f)
            {
                todoScrollY =
                    ((mousePos.y - scrollTrack.y - thumbHeight * 0.5f) / thumbTravel) * maxScroll;
            }
        }

        if (todoScrollbarDragging)
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && thumbTravel > 0.0f)
            {
                todoScrollY =
                    ((mousePos.y - scrollTrack.y - todoScrollbarDragOffsetY) / thumbTravel) * maxScroll;
            }
            else
            {
                todoScrollbarDragging = false;
            }
        }

        if (CheckCollisionPointRec(mousePos, content))
            todoScrollY -= GetMouseWheelMove() * lineHeight * 3.0f;

        todoScrollY = std::max(0.0f, std::min(todoScrollY, maxScroll));
    }

    void Location::drawMainImage() const
    {
        const Rectangle mainBounds = getMainImageBounds();

        if (inventoryMgr.isOpen() && inventoryMgr.isExaminingItem())
        {
            const InventoryItem* item = inventoryMgr.getSelectedItem();
            if (item != nullptr && item->examineImage.id != 0 && IsTextureValid(item->examineImage))
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
            DrawRectangleRec(mainBounds, Color{48, 44, 56, 255});
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
                Color{186, 150, 72, 255});
        }
    }

    bool Location::isSidePanelOpen() const
    {
        return inventoryMgr.isOpen() || takeMgr.isOpen() || interactionMgr.isOpen();
    }

    std::string Location::takenItemKey(const std::string& itemId) const
    {
        return currentSceneId + ":" + itemId;
    }

    std::vector<TakeableItemDef> Location::getDroppedItemsInCurrentScene() const
    {
        const std::map<std::string, std::vector<TakeableItemDef>>::const_iterator it =
            droppedItemsByScene.find(currentSceneId);
        if (it == droppedItemsByScene.end())
            return std::vector<TakeableItemDef>();

        return it->second;
    }

    bool Location::isDroppedItemInCurrentScene(const std::string& itemId) const
    {
        for (const TakeableItemDef& item : getDroppedItemsInCurrentScene())
        {
            if (item.id == itemId)
                return true;
        }

        return false;
    }

    void Location::removeDroppedItem(const std::string& sceneId, const std::string& itemId)
    {
        std::map<std::string, std::vector<TakeableItemDef>>::iterator sceneIt =
            droppedItemsByScene.find(sceneId);
        if (sceneIt == droppedItemsByScene.end())
            return;

        std::vector<TakeableItemDef>& items = sceneIt->second;
        for (std::vector<TakeableItemDef>::iterator it = items.begin(); it != items.end(); ++it)
        {
            if (it->id == itemId)
            {
                items.erase(it);
                break;
            }
        }

        if (items.empty())
            droppedItemsByScene.erase(sceneIt);
    }

    TakeableItemDef Location::takeableFromInventory(const InventoryItem& item) const
    {
        TakeableItemDef takeable;
        takeable.id = item.id;
        takeable.name = item.name.empty() ? item.id : item.name;
        takeable.iconPath = item.iconPath;
        takeable.examineImagePath = item.examineImagePath;
        takeable.examineText = item.examineText;
        takeable.requiresExamine = true;
        return takeable;
    }

    void Location::dropInventoryItem(const std::string& itemId)
    {
        const InventoryItem* item = inventoryMgr.getItemById(itemId);
        if (item == nullptr)
            return;

        removeDroppedItem(currentSceneId, itemId);
        droppedItemsByScene[currentSceneId].push_back(takeableFromInventory(*item));

        if (!inventoryMgr.removeItem(itemId))
            return;

        refreshTakeItems();
        updateActionAvailability();
    }

    void Location::handleInventoryDropInput()
    {
        const std::string itemId = inventoryMgr.consumePendingDropItemId();
        if (itemId.empty() || !inventoryMgr.hasItem(itemId))
            return;

        if (gameConfig.input.skipDropConfirmation)
        {
            dropInventoryItem(itemId);
            return;
        }

        const InventoryItem* item = inventoryMgr.getItemById(itemId);
        if (item == nullptr)
            return;

        const std::string itemName = item->name.empty() ? item->id : item->name;
        dropConfirmMgr.openForItem(itemId, itemName);
    }

    std::string Location::buildExamineDetailsWithDroppedItems() const
    {
        std::string details = examineDetails;
        const std::vector<TakeableItemDef> droppedItems = getDroppedItemsInCurrentScene();

        for (const TakeableItemDef& item : droppedItems)
        {
            const std::string label = item.name.empty() ? item.id : item.name;
            details += "\n\nYou notice someone has discarded ";
            details += label;
            details += ".";
        }

        return details;
    }

    std::vector<TakeableItemDef> Location::getAvailableTakeables() const
    {
        std::vector<TakeableItemDef> available;
        const std::vector<TakeableItemDef>& takeables = sceneDatabase.getTakeables(currentSceneId);

        for (const TakeableItemDef& item : takeables)
        {
            if (takenItemKeys.count(takenItemKey(item.id)) > 0)
                continue;

            if (inventoryMgr.hasItem(item.id))
                continue;

            if (item.requiresExamine && !hasExaminedScene(currentSceneId))
                continue;

            available.push_back(item);
        }

        for (const TakeableItemDef& item : getDroppedItemsInCurrentScene())
        {
            if (inventoryMgr.hasItem(item.id))
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

    InventoryItem Location::buildInventoryItem(
        const std::string& defId,
        const ItemDefOverrides& overrides) const
    {
        std::vector<std::string> flags(storyFlags.begin(), storyFlags.end());
        ItemInstance instance = itemDatabase.hasDef(defId)
            ? itemDatabase.createInstance(defId, flags)
            : ItemInstance{};
        if (!itemDatabase.hasDef(defId))
        {
            instance.instanceId = defId;
            instance.defId = defId;
        }

        return itemDatabase.buildInventoryItem(instance, overrides);
    }

    void Location::playItemExamineAudio(const InventoryItem& item)
    {
        const ItemDef* def = itemDatabase.getDef(item.id);
        if (def == nullptr)
            return;

        if (def->examineTts.enabled && gameConfig.tts.enabled)
        {
            if (!def->examineTts.audio.empty())
                audioManager.playDialogAsset(def->examineTts.audio);
            else
                TraceLog(LOG_WARNING, "Item %s has examine TTS enabled but no bundled audio", item.id.c_str());
        }

        if (!def->sfx.examine.empty())
        {
            AudioClipDef examineSfx;
            examineSfx.path = def->sfx.examine;
            examineSfx.loop = false;
            audioManager.playSfx(examineSfx);
        }

        if (def->examineAudio.hasMusic || !def->examineAudio.ambient.empty() || def->examineAudio.muteSceneAudio)
            audioManager.applyItemExamineAudio(def->examineAudio);
    }

    void Location::clearItemExamineAudio()
    {
        audioManager.clearItemExamineAudio();
    }

    bool Location::canTakeFromExaminedItem() const
    {
        return inventoryMgr.isOpen()
            && inventoryMgr.isExaminingItem()
            && inventoryMgr.canExtractFromExaminedItem(itemDatabase);
    }

    void Location::takeFromExaminedItem()
    {
        InventoryItem extracted;
        if (!inventoryMgr.extractFromExaminedItem(itemDatabase, extracted))
            return;

        if (extracted.id.empty())
            return;

        inventoryMgr.addItem(extracted);
        evaluateMilestones();
    }

    void Location::addTakenItemToInventory(const TakeableItemDef& taken)
    {
        if (inventoryMgr.hasItem(taken.id))
            return;

        ItemDefOverrides overrides;
        overrides.name = taken.name;
        overrides.description = taken.examineText;
        overrides.iconPath = taken.iconPath;
        overrides.examineImagePath = taken.examineImagePath;

        inventoryMgr.addItem(buildInventoryItem(taken.id, overrides));
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
                if (isDroppedItemInCurrentScene(taken.id))
                    removeDroppedItem(currentSceneId, taken.id);
                else
                    takenItemKeys.insert(takenItemKey(taken.id));

                addTakenItemToInventory(taken);
            }
            refreshTakeItems();
            if (takeMgr.isEmpty())
            {
                takeMgr.close();
                updateInventoryLayout();
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

        if (isDroppedItemInCurrentScene(taken.id))
            removeDroppedItem(currentSceneId, taken.id);
        else
            takenItemKeys.insert(takenItemKey(taken.id));

        addTakenItemToInventory(taken);
        refreshTakeItems();
        if (takeMgr.isEmpty())
        {
            takeMgr.close();
            updateInventoryLayout();
        }
        updateActionAvailability();
    }

    Rectangle Location::getDialogBounds() const
    {
        const float height = isSidePanelOpen()
            ? fullDialogHeight * kDialogHeightShareWhenSidePanelOpen
            : fullDialogHeight;
        return { textBox.x, textBox.y, textBox.width, height };
    }

    Rectangle Location::getInventoryPanelBounds() const
    {
        const Rectangle dialog = getDialogBounds();
        return {
            textBox.x,
            dialog.y + dialog.height,
            textBox.width,
            fullDialogHeight * kSidePanelHeightShare
        };
    }

    void Location::updateInventoryLayout()
    {
        if (inventoryMgr.isOpen() || takeMgr.isOpen() || interactionMgr.isOpen())
        {
            const Rectangle panelBounds = getInventoryPanelBounds();
            if (inventoryMgr.isOpen())
                inventoryMgr.setPanelBounds(panelBounds);
            if (takeMgr.isOpen())
                takeMgr.setPanelBounds(panelBounds);
            if (interactionMgr.isOpen())
                interactionMgr.setPanelBounds(panelBounds);
        }
    }

    Rectangle Location::getNotebookContentBounds() const
    {
        const Rectangle dialog = getDialogBounds();
        const float contentTop = dialog.y + notebookHeaderReserve;
        const float contentHeight = dialog.height - notebookHeaderReserve - notebookContentBottomPad;
        return {
            dialog.x,
            contentTop,
            dialog.width - kScrollbarWidth,
            std::max(0.0f, contentHeight)
        };
    }

    float Location::getNarrativeVisibleHeight() const
    {
        return getNotebookContentBounds().height;
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

    std::string Location::interactionKey(const std::string& interactionId) const
    {
        return currentSceneId + ":" + interactionId;
    }

    std::vector<SceneInteractionDef> Location::getAvailableInteractions() const
    {
        std::vector<SceneInteractionDef> available;
        const std::vector<SceneInteractionDef>& interactions =
            sceneDatabase.getInteractions(currentSceneId);

        for (const SceneInteractionDef& interaction : interactions)
        {
            if (interaction.requiresExamine && !hasExaminedScene(currentSceneId))
                continue;

            if (!interaction.repeat &&
                usedInteractionKeys.count(interactionKey(interaction.id)) > 0)
            {
                continue;
            }

            available.push_back(interaction);
        }

        return available;
    }

    bool Location::canUseInCurrentScene() const
    {
        if (!getAvailableInteractions().empty())
            return true;

        if (useDetails.empty() && useExit.empty())
            return false;

        if (useRequiresExamine && !hasExaminedScene(currentSceneId))
            return false;

        if (useRepeatStatus)
            return !hasUsedInCurrentScene;

        return usedSceneIds.count(currentSceneId) == 0;
    }

    std::string Location::normalizeNarrativeLine(std::string line) const
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return line;
    }

    bool Location::isDialogChoiceLine(const std::string& line) const
    {
        return line.size() > 2 && line.compare(0, 2, "> ") == 0;
    }

    bool Location::isNarrativeSketchLine(const std::string& line) const
    {
        const size_t prefixLen = sizeof(kNarrativeSketchPrefix) - 1;
        return line.size() > prefixLen && line.compare(0, prefixLen, kNarrativeSketchPrefix) == 0;
    }

    std::string Location::narrativeSketchPathFromLine(const std::string& line) const
    {
        if (!isNarrativeSketchLine(line))
            return "";

        const size_t prefixLen = sizeof(kNarrativeSketchPrefix) - 1;
        return line.substr(prefixLen);
    }

    float Location::getNarrativeSketchDisplaySize(
        const Texture2D& texture,
        float& outWidth,
        float& outHeight) const
    {
        if (texture.id == 0 || texture.width <= 0 || texture.height <= 0)
        {
            outWidth = 0.0f;
            outHeight = 0.0f;
            return 0.0f;
        }

        const float maxWidth = getNarrativeWrapWidth();
        const float maxHeight = std::max(getNarrativeLineHeight() * 5.0f, 220.0f);
        const float widthScale = maxWidth / (float)texture.width;
        const float heightScale = maxHeight / (float)texture.height;
        const float scale = std::min(widthScale, heightScale);

        outWidth = (float)texture.width * scale;
        outHeight = (float)texture.height * scale;
        return outHeight + getNarrativeLineHeight() * 0.5f;
    }

    Texture2D Location::getOrLoadNarrativeSketchTexture(const std::string& sketchPath) const
    {
        if (sketchPath.empty())
            return {};

        const auto cached = narrativeSketchTextures.find(sketchPath);
        if (cached != narrativeSketchTextures.end())
            return cached->second;

        Texture2D texture{};
        if (!loadResourceTexture(sceneDatabase.getAssetRoot(), sketchPath, texture))
        {
            const std::vector<std::string> paths = buildAssetSearchPaths(
                sceneDatabase.getAssetRoot(),
                sketchPath);
            for (const std::string& path : paths)
            {
                if (!FileExists(path.c_str()))
                    continue;

                texture = LoadTexture(path.c_str());
                if (texture.id != 0)
                    break;

                if (loadTextureFromAssetFile(path, texture))
                    break;
            }
        }

        if (texture.id != 0)
        {
            SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
            narrativeSketchTextures[sketchPath] = texture;
            TraceLog(LOG_INFO, "Loaded narrative sketch: %s (%dx%d)", sketchPath.c_str(), texture.width, texture.height);
            return texture;
        }

        TraceLog(LOG_WARNING, "Failed to load narrative sketch: %s", sketchPath.c_str());
        return {};
    }

    float Location::getNarrativeSketchHeight(const std::string& sketchPath) const
    {
        const Texture2D texture = getOrLoadNarrativeSketchTexture(sketchPath);
        float drawWidth = 0.0f;
        float drawHeight = 0.0f;
        return getNarrativeSketchDisplaySize(texture, drawWidth, drawHeight);
    }

    void Location::layoutNarrativeSketch(const std::string& sketchPath, float& textOffsetY) const
    {
        const Texture2D texture = getOrLoadNarrativeSketchTexture(sketchPath);
        float drawWidth = 0.0f;
        float drawHeight = 0.0f;
        const float blockHeight = getNarrativeSketchDisplaySize(texture, drawWidth, drawHeight);
        if (texture.id == 0 || blockHeight <= 0.0f)
            return;

        NarrativeSketchPlacement placement;
        placement.path = sketchPath;
        placement.yOffset = textOffsetY;
        placement.width = drawWidth;
        placement.height = drawHeight;
        narrativeSketchPlacements.push_back(placement);
        textOffsetY += blockHeight;
    }

    void Location::drawNarrativeSketches() const
    {
        const Rectangle content = getNotebookContentBounds();
        const Rectangle dialog = getDialogBounds();
        const float visibleHeight = content.height;

        for (const NarrativeSketchPlacement& placement : narrativeSketchPlacements)
        {
            const Texture2D texture = getOrLoadNarrativeSketchTexture(placement.path);
            if (texture.id == 0)
                continue;

            const float drawY = content.y + placement.yOffset - narrativeScrollY;
            const bool visible = (placement.yOffset + placement.height > narrativeScrollY) &&
                                 (placement.yOffset < narrativeScrollY + visibleHeight);
            if (!visible)
                continue;

            const Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height };
            const Rectangle dest = {
                dialog.x + xOffset,
                drawY,
                placement.width,
                placement.height
            };
            DrawTexturePro(texture, source, dest, { 0.0f, 0.0f }, 0.0f, WHITE);
        }
    }

    void Location::appendNarrativeSketch(const std::string& sketchPath)
    {
        if (sketchPath.empty())
            return;

        getOrLoadNarrativeSketchTexture(sketchPath);
        narrativeText += "\n";
        narrativeText += kNarrativeSketchPrefix;
        narrativeText += sketchPath;
        trimNarrativeBuffer();
        narrativeLayoutDirty = true;
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

    void Location::evaluateMilestones()
    {
        ConversationPersistState conversationSnapshot;
        conversationMgr.exportPersistState(conversationSnapshot);

        MilestoneEventContext context;
        context.storyFlags = storyFlags;
        context.conversation = &conversationSnapshot;
        context.sceneId = currentSceneId;
        context.hasItem = [this](const std::string& itemId)
        {
            return inventoryMgr.hasItem(itemId);
        };
        context.getMilestoneStatus = [this](const std::string& milestoneId)
        {
            return milestoneMgr.getStatus(milestoneId);
        };

        milestoneMgr.evaluate(context);
    }

    void Location::appendExamineDetails()
    {
        const std::string details = buildExamineDetailsWithDroppedItems();
        if (details.empty())
            return;

        appendNarrativeSection("Examining:", details);
        if (!examineFlag.empty())
            storyFlags.insert(examineFlag);
        examinedSceneIds.insert(currentSceneId);
        evaluateMilestones();
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

    std::vector<ConversationChoiceDef> Location::filterAvailableChoices(
        const std::vector<ConversationChoiceDef>& choices) const
    {
        std::vector<ConversationChoiceDef> available;
        available.reserve(choices.size());

        for (const ConversationChoiceDef& choice : choices)
        {
            if (choice.isAvailable(walletCash))
                available.push_back(choice);
        }

        return available;
    }

    void Location::appendChoiceLinesToNarrative(const std::vector<ConversationChoiceDef>& choices)
    {
        const std::vector<ConversationChoiceDef> available = filterAvailableChoices(choices);
        for (const ConversationChoiceDef& choice : available)
        {
            narrativeText += "\n";
            narrativeText += choice.label;
        }

        trimNarrativeBuffer();
        narrativeLayoutDirty = true;
        updateActionAvailability();

        if (!available.empty())
            scrollToPendingDialogChoices();
    }

    void Location::scrollToPendingDialogChoices()
    {
        const std::vector<ConversationChoiceDef>& choices = conversationMgr.getPendingChoices();
        if (choices.empty())
            return;

        rebuildNarrativeLayout();

        if (!narrativeSketchPlacements.empty())
        {
            const NarrativeSketchPlacement& placement = narrativeSketchPlacements.back();
            const float maxScroll = std::max(0.0f, narrativeContentHeight - getNarrativeVisibleHeight());
            narrativeScrollY = std::max(0.0f, std::min(placement.yOffset, maxScroll));
            return;
        }

        scrollNarrativeToLine(choices.front().label, true);
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

    void Location::grantConversationItem(const GrantedInventoryItemDef& granted)
    {
        if (!granted.isValid() || inventoryMgr.hasItem(granted.id))
            return;

        ItemDefOverrides overrides;
        overrides.name = granted.name;
        overrides.description = granted.examineText;
        overrides.iconPath = granted.iconPath;
        overrides.examineImagePath = granted.examineImagePath;

        inventoryMgr.addItem(buildInventoryItem(granted.id, overrides));
        evaluateMilestones();
    }

    void Location::playDialogAudio(const SpeakResult& result)
    {
        if (result.useTts && !result.ttsAudioPaths.empty())
        {
            if (!gameConfig.tts.enabled)
                return;

            if (audioManager.playDialogAssetSequence(result.ttsAudioPaths))
                return;

            TraceLog(
                LOG_WARNING,
                "Missing bundled TTS audio (run with --refresh-voices=API_KEY)");
            return;
        }

        if (!result.dialogAudioTracks.empty())
            audioManager.playDialogSequence(result.dialogAudioTracks);
    }

    void Location::processSpeakResult(const SpeakResult& result)
    {
        if (result.action == SpeakResult::Action::None
            && result.narrative.empty()
            && result.sketchPath.empty())
            return;

        if (!result.sketchPath.empty())
            appendNarrativeSketch(result.sketchPath);

        if (!result.narrative.empty())
            appendNarrativeSection("Speaking:", result.narrative);

        if (result.action == SpeakResult::Action::ShowChoices)
        {
            applyStatusEffects(result.statusEffects);
            playDialogAudio(result);
            const std::vector<ConversationChoiceDef>& pending = conversationMgr.getPendingChoices();
            appendChoiceLinesToNarrative(!pending.empty() ? pending : result.choices);
            evaluateMilestones();
            return;
        }

        applyStatusEffects(result.statusEffects);
        grantConversationItem(result.grantItem);
        playDialogAudio(result);
        evaluateMilestones();
    }

    void Location::resolveDialogChoice(const std::string& choiceId)
    {
        if (!conversationMgr.isAwaitingChoice())
            return;

        const std::vector<ConversationChoiceDef> choicesToStrip = conversationMgr.getPendingChoices();

        std::string selectedLineText;
        std::string selectedSketchPath;
        const ConversationChoiceDef* selectedChoice = nullptr;
        for (const ConversationChoiceDef& choice : choicesToStrip)
        {
            if (choice.id == choiceId)
            {
                selectedChoice = &choice;
                selectedLineText = choice.label;
                selectedSketchPath = choice.sketchPath;
                break;
            }
        }

        if (selectedChoice != nullptr && !selectedChoice->isAvailable(walletCash))
            return;

        const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(currentSceneId);
        SpeakResult result = conversationMgr.resolveChoice(speakConfig, choiceId);
        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            result = conversationMgr.resolveChoiceFromConfig(speakConfig, choiceId);

        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            return;

        const std::string responseText = result.narrative;
        const std::vector<StatusEffect> effects = result.statusEffects;

        narrativeChoiceHitAreas.clear();

        if (!selectedLineText.empty())
            committedPlayerDialogLines.insert(selectedLineText);

        stripDialogChoiceLinesFromNarrative(choicesToStrip, selectedLineText);

        if (!selectedSketchPath.empty())
            appendNarrativeSketch(selectedSketchPath);

        if (!responseText.empty())
        {
            narrativeText += "\n\n";
            narrativeText += responseText;
            trimNarrativeBuffer();
            narrativeLayoutDirty = true;
        }

        if (result.action == SpeakResult::Action::ShowChoices)
        {
            applyStatusEffects(effects);
            playDialogAudio(result);
            const std::vector<ConversationChoiceDef>& pending = conversationMgr.getPendingChoices();
            appendChoiceLinesToNarrative(!pending.empty() ? pending : result.choices);
            updateActionAvailability();
            evaluateMilestones();
            return;
        }

        applyStatusEffects(effects);
        grantConversationItem(result.grantItem);
        playDialogAudio(result);

        if (!responseText.empty())
        {
            rebuildNarrativeLayout();
            std::istringstream responseStream(responseText);
            std::string firstLine;
            if (std::getline(responseStream, firstLine) && !firstLine.empty())
                scrollNarrativeToLine(firstLine, true);
        }

        updateActionAvailability();
        evaluateMilestones();
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
        droppedItemsByScene.clear();
        takeMgr.close();
        interactionMgr.close();
        inventoryMgr.close();
        dropConfirmMgr.close();
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

        const Rectangle textArea = getNotebookContentBounds();
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

    void Location::transitionToScene(const std::string& nextSceneId)
    {
        if (nextSceneId.empty())
            return;

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

        interactionMgr.close();
        takeMgr.close();
    }

    void Location::applyInteraction(const SceneInteractionDef& interaction)
    {
        if (!interaction.useDetails.empty())
            appendNarrativeSection("Using:", interaction.useDetails);

        StatusEffect useEffect;
        useEffect.key = interactionKey(interaction.id);
        useEffect.health = interaction.useHealthDelta;
        useEffect.energy = interaction.useEnergyDelta;
        tryApplyStatusEffect(useEffect, interaction.repeat);

        if (!interaction.repeat)
            usedInteractionKeys.insert(interactionKey(interaction.id));

        interactionMgr.close();
        updateInventoryLayout();

        if (!interaction.exitSceneId.empty())
            transitionToScene(interaction.exitSceneId);
        else
            updateActionAvailability();
    }

    void Location::applyDirectUse()
    {
        if (!canUseInCurrentScene())
            return;

        if (!useDetails.empty())
            appendNarrativeSection("Using:", useDetails);

        StatusEffect useEffect;
        useEffect.key = currentSceneId + ":use";
        useEffect.health = useHealthDelta;
        useEffect.energy = useEnergyDelta;
        tryApplyStatusEffect(useEffect, useRepeatStatus);
        hasUsedInCurrentScene = true;
        if (!useRepeatStatus)
            usedSceneIds.insert(currentSceneId);

        const std::string exitSceneId = useExit;
        if (!exitSceneId.empty())
            transitionToScene(exitSceneId);
        else
            updateActionAvailability();
    }

    void Location::refreshInteractions()
    {
        interactionMgr.setAvailableInteractions(getAvailableInteractions());
    }

    void Location::processPendingInteractions()
    {
        const std::string interactionId = interactionMgr.consumePendingInteractionId();
        if (interactionId.empty())
            return;

        const std::vector<SceneInteractionDef> available = getAvailableInteractions();
        for (const SceneInteractionDef& interaction : available)
        {
            if (interaction.id != interactionId)
                continue;

            applyInteraction(interaction);
            scrollNarrativeToHeader("Using:");
            return;
        }
    }

    void Location::appendUseDetails()
    {
        applyDirectUse();
        scrollNarrativeToHeader("Using:");
    }

    void Location::updateActionAvailability()
    {
        MovementStruct movement{};
        ActionStruct actions{};

        if (dropConfirmMgr.isOpen())
        {
            buttonMgr.setAvailability(movement, actions);
            buttonMgr.setStatus(health, energy, resolve, lucidity, charisma);
            return;
        }

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
        else if (interactionMgr.isOpen())
        {
            movement.up = false;
            movement.down = false;
            movement.forward = false;
            movement.left = false;
            movement.right = false;
            movement.backward = false;
            actions.use = true;
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
            {
                movement.backward = true;
                if (canTakeFromExaminedItem())
                    actions.take = true;
            }
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
            line = normalizeNarrativeLine(line);
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

            if (isNarrativeSketchLine(line))
            {
                textOffsetY += getNarrativeSketchHeight(narrativeSketchPathFromLine(line));
                continue;
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
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (isNarrativeSketchLine(line))
            {
                textOffsetY += getNarrativeSketchHeight(narrativeSketchPathFromLine(line));
                continue;
            }

            if (isDialogChoiceLine(line))
            {
                for (const ConversationChoiceDef& choice :
                    filterAvailableChoices(conversationMgr.getPendingChoices()))
                {
                    if (line != choice.label)
                        continue;

                    const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
                    float measureY = textOffsetY;
                    layoutWrappedParagraph(line.c_str(), lineFont, fontSize, measureY, false, 0.0f, textColor);
                    const float choiceHeight = std::max(getNarrativeLineHeight(), measureY - textOffsetY);
                    const float drawY = getNotebookContentBounds().y + textOffsetY - narrativeScrollY;

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
    
    bool Location::hasLightSourceInInventory() const
    {
        for (const InventoryItem& item : inventoryMgr.exportItemSnapshots())
        {
            const ItemDef* def = itemDatabase.getDef(item.id);
            if (def != nullptr && def->lightSource)
                return true;
        }

        return false;
    }

    bool Location::canUseExit(const std::string& direction, std::string& outBlockedDetails) const
    {
        ExitRequirementDef requirement;
        if (!sceneDatabase.getExitRequirement(currentSceneId, direction, requirement))
            return true;

        if (requirement.requiresLightSource && !hasLightSourceInInventory())
        {
            outBlockedDetails = requirement.blockedDetails;
            return false;
        }

        return true;
    }

    void Location::appendBlockedMovementMessage(const std::string& details)
    {
        if (details.empty())
            return;

        narrativeText += "\n\n";
        narrativeText += details;
        trimNarrativeBuffer();
        narrativeLayoutDirty = true;
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

        std::string blockedDetails;
        if (!canUseExit(direction, blockedDetails))
        {
            appendBlockedMovementMessage(blockedDetails);
            scrollNarrativeToLine(blockedDetails, true);
            return;
        }

        transitionToScene(nextSceneId);
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
        useRequiresExamine = locationStruct.useRequiresExamine;
        useExit = locationStruct.useExit;
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
        evaluateMilestones();
        updateActionAvailability();
    }

    void Location::rebuildNarrativeLayout() const
    {
        narrativeContentHeight = 0.0f;
        narrativeSketchPlacements.clear();

        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (isNarrativeSketchLine(line))
            {
                layoutNarrativeSketch(narrativeSketchPathFromLine(line), textOffsetY);
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

        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const float maxScroll = std::max(0.0f, narrativeContentHeight - visibleHeight);
        const float lineHeight = getNarrativeLineHeight();

        const Rectangle dialog = getDialogBounds();
        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
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

        if (CheckCollisionPointRec(mousePos, content))
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
        state.usedInteractionKeys = usedInteractionKeys;
        state.storyFlags = storyFlags;
        state.consumedStatusActions = consumedStatusActions;
        state.committedPlayerDialogLines = committedPlayerDialogLines;
        state.inventoryItems = inventoryMgr.exportItemSnapshots();
        state.droppedItemsByScene = droppedItemsByScene;
        conversationMgr.exportPersistState(state.conversation);
        milestoneMgr.exportPersistState(state.milestones);
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
        usedInteractionKeys = state.usedInteractionKeys;
        storyFlags = state.storyFlags;
        consumedStatusActions = state.consumedStatusActions;
        committedPlayerDialogLines = state.committedPlayerDialogLines;
        narrativeScrollY = 0.0f;
        narrativeLayoutDirty = true;

        inventoryMgr.restoreFromSnapshots(state.inventoryItems);
        droppedItemsByScene = state.droppedItemsByScene;
        dropConfirmMgr.close();
        conversationMgr.importPersistState(state.conversation);
        milestoneMgr.importPersistState(state.milestones);
        takeMgr.close();
        narrativeChoiceHitAreas.clear();

        ConversationPersistState conversationSnapshot;
        conversationMgr.exportPersistState(conversationSnapshot);
        milestoneMgr.syncFromLegacyState(
            storyFlags,
            conversationSnapshot,
            [this](const std::string& itemId) { return inventoryMgr.hasItem(itemId); });
        evaluateMilestones();

        audioManager.onRoomEnter(sceneDatabase.getSceneAudio(currentSceneId), fromSceneId);
        trimNarrativeBuffer();
        updateInventoryLayout();
        updateActionAvailability();
        return true;
    }

    namespace
    {
        std::string trimSaveName(const std::string& value)
        {
            size_t start = 0;
            size_t end = value.size();
            while (start < end && std::isspace(static_cast<unsigned char>(value[start])))
                ++start;
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
                --end;
            return value.substr(start, end - start);
        }
    }

    bool Location::quickSaveToDisk()
    {
        SaveSlotMetadata metadata;
        metadata.name = "Quick Save";
        metadata.timestampIso = currentTimestampIso(metadata.unixTime);
        metadata.isQuickSave = true;
        return writeSaveFile(quickSavePath(), captureSaveState(), metadata);
    }

    bool Location::namedSaveToDisk(const std::string& saveName)
    {
        const std::string trimmedName = trimSaveName(saveName);
        if (trimmedName.empty())
            return false;

        enforceNamedSaveLimit(gameConfig.saves.maxNamedSaves);

        SaveSlotMetadata metadata;
        metadata.name = trimmedName;
        metadata.timestampIso = currentTimestampIso(metadata.unixTime);
        metadata.isQuickSave = false;
        return writeSaveFile(buildNamedSavePath(metadata.unixTime), captureSaveState(), metadata);
    }

    bool Location::loadGameFromPath(const std::string& path)
    {
        SavedGameState state;
        if (!readSaveFile(path, state))
            return false;

        return applySaveState(state);
    }

    void Location::showTransientMessage(const std::string& message, float durationSeconds)
    {
        transientMessage = message;
        transientMessageTimer = durationSeconds;
    }

    void Location::updateTransientMessage(float deltaSeconds)
    {
        if (transientMessageTimer <= 0.0f)
            return;

        transientMessageTimer -= deltaSeconds;
        if (transientMessageTimer <= 0.0f)
            transientMessage.clear();
    }

    void Location::drawTransientMessage() const
    {
        if (transientMessage.empty())
            return;

        const Vector2 textSize = MeasureTextEx(
            descriptionFont,
            transientMessage.c_str(),
            fontSize,
            spacing);
        const float padX = 28.0f;
        const float padY = 16.0f;
        const float panelWidth = textSize.x + padX * 2.0f;
        const float panelHeight = textSize.y + padY * 2.0f;
        const Rectangle panel = {
            (screenWidth - panelWidth) * 0.5f,
            5.0f,
            panelWidth,
            panelHeight
        };

        DrawRectangleRounded(panel, 0.2f, 8, {28, 26, 34, 230});
        DrawRoundedBorder(panel, 0.2f, 8, 2.0f, {168, 138, 72, 255});
        DrawTextEx(
            descriptionFont,
            transientMessage.c_str(),
            { panel.x + padX, panel.y + padY },
            fontSize,
            spacing,
            {228, 220, 198, 255});
    }

    void Location::handleQuickSaveInput()
    {
        if (!IsKeyPressed(KEY_F5))
            return;

        if (saveLoadMenu.isOpen() || pauseMenu.isOpen() || isSidePanelOpen())
            return;

        if (quickSaveToDisk())
            showTransientMessage("Quick save created.");
        else
            showTransientMessage("Quick save failed.");
    }

    void Location::handleSaveLoadMenuInput()
    {
        if (IsKeyPressed(KEY_ESCAPE))
            saveLoadMenu.closeMenu();

        if (saveLoadMenu.consumeBackRequest())
            saveLoadMenu.closeMenu();

        std::string saveName;
        if (saveLoadMenu.consumeNamedSaveRequest(saveName))
        {
            if (namedSaveToDisk(saveName))
                pauseMenu.setStatusMessage("Game saved.");
            else
                pauseMenu.setStatusMessage("Save failed.");

            saveLoadMenu.closeMenu();
        }

        std::string loadPath;
        if (saveLoadMenu.consumeLoadSlotRequest(loadPath))
        {
            if (loadGameFromPath(loadPath))
            {
                pauseMenu.setStatusMessage("Game loaded.");
                saveLoadMenu.closeMenu();
                pauseMenu.closeMenu();
                audioManager.setGameplayPaused(false);
            }
            else
                pauseMenu.setStatusMessage("Load failed.");
        }
    }

    void Location::handlePauseMenuInput()
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            if (saveLoadMenu.isOpen())
            {
                saveLoadMenu.closeMenu();
            }
            else if (pauseMenu.isOpen())
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

        if (!saveLoadMenu.isOpen())
            pauseMenu.update(GetFrameTime());

        if (pauseMenu.consumeOpenSaveMenuRequest())
            saveLoadMenu.openSaveMenu();

        if (pauseMenu.consumeOpenLoadMenuRequest())
            saveLoadMenu.openLoadMenu();

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
        updateTransientMessage(GetFrameTime());
        handleQuickSaveInput();
        handlePauseMenuInput();

        if (saveLoadMenu.isOpen())
        {
            saveLoadMenu.update();
            handleSaveLoadMenuInput();
            return;
        }

        if (pauseMenu.isOpen())
            return;

        if (dropConfirmMgr.isOpen())
        {
            dropConfirmMgr.update();

            const std::string confirmedItemId = dropConfirmMgr.consumeConfirmedItemId();
            if (!confirmedItemId.empty())
            {
                if (dropConfirmMgr.consumeDontShowAgainRequest())
                {
                    gameConfig.input.skipDropConfirmation = true;
                    saveGameConfig(gameConfigPath, gameConfig);
                }

                dropInventoryItem(confirmedItemId);
            }

            dropConfirmMgr.consumeCancelled();
            return;
        }

        updateInventoryLayout();
        updateActionAvailability();

        if (!inventoryMgr.isExaminingItem() && notebookPage == NotebookPage::CaseNotes)
            handleNarrativeChoiceInput();

        handleNotebookNavInput();

        buttonMgr.update();

        if (buttonMgr.consumeMoveOrActionButtonClick())
            audioManager.stopDialog();

        if (buttonMgr.consumeInventoryButtonClick())
        {
            if (inventoryMgr.isOpen())
                inventoryMgr.close();
            else
            {
                if (takeMgr.isOpen())
                    takeMgr.close();
                if (interactionMgr.isOpen())
                    interactionMgr.close();
                inventoryMgr.open();
            }

            updateInventoryLayout();
            updateActionAvailability();
        }

        if (buttonMgr.consumeTakeButtonClick())
        {
            if (canTakeFromExaminedItem())
            {
                takeFromExaminedItem();
                updateActionAvailability();
            }
            else if (takeMgr.isOpen())
                takeMgr.close();
            else
            {
                if (inventoryMgr.isOpen())
                    inventoryMgr.close();
                if (interactionMgr.isOpen())
                    interactionMgr.close();
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

        if (interactionMgr.isOpen())
        {
            interactionMgr.update();
            processPendingInteractions();
            handleNarrativeScrollInput();
            updateActionAvailability();
            return;
        }

        if (inventoryMgr.isOpen())
        {
            inventoryMgr.update();
            handleInventoryDropInput();

            if (buttonMgr.consumeBackwardButtonClick() && inventoryMgr.isExaminingItem())
            {
                clearItemExamineAudio();
                inventoryMgr.returnToItemList();
                inventoryExamineScrollY = 0.0f;
                updateActionAvailability();
            }
            else if (buttonMgr.consumeExamineButtonClick() && inventoryMgr.canExamineSelectedItem())
            {
                inventoryMgr.examineSelectedItem();
                const InventoryItem* examinedItem = inventoryMgr.getSelectedItem();
                if (examinedItem != nullptr)
                    playItemExamineAudio(*examinedItem);
                inventoryExamineScrollY = 0.0f;
                updateActionAvailability();
            }
            if (inventoryMgr.isExaminingItem())
                handleInventoryExamineScrollInput();
            else if (notebookPage == NotebookPage::Todo)
                handleTodoScrollInput();
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
            if (!conversationMgr.isAwaitingChoice())
                scrollNarrativeToHeader("Speaking:");
        }

        if (!conversationMgr.isAwaitingChoice() && buttonMgr.consumeUseButtonClick())
        {
            if (!getAvailableInteractions().empty())
            {
                if (interactionMgr.isOpen())
                    interactionMgr.close();
                else
                {
                    if (inventoryMgr.isOpen())
                        inventoryMgr.close();
                    if (takeMgr.isOpen())
                        takeMgr.close();
                    refreshInteractions();
                    interactionMgr.open();
                    updateInventoryLayout();
                }
                updateActionAvailability();
            }
            else
            {
                appendUseDetails();
            }
        }

        if (notebookPage == NotebookPage::Todo)
            handleTodoScrollInput();
        else
            handleNarrativeScrollInput();
    }

    void Location::handleInventoryExamineScrollInput()
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const float maxScroll = std::max(0.0f, inventoryExamineContentHeight - visibleHeight);
        const float lineHeight = getNarrativeLineHeight();

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
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

        if (CheckCollisionPointRec(mousePos, content))
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
        else if (notebookPage == NotebookPage::Todo)
        {
            drawQuestTodoPage();
            drawTodoScrollbar();
        }
        else
        {
            drawNarrativeText();
            drawNarrativeScrollbar();
        }

        drawNotebookHeader(dialog);
        drawNotebookNavButtons(dialog);

        if (inventoryMgr.isOpen())
            inventoryMgr.draw();
        else if (takeMgr.isOpen())
            takeMgr.draw();
        else if (interactionMgr.isOpen())
            interactionMgr.draw();

        buttonMgr.draw();
        pauseMenu.draw();
        saveLoadMenu.draw();
        drawTransientMessage();
        dropConfirmMgr.draw();
    }

    void Location::drawInventoryExamineScrollbar() const
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const float maxScroll = std::max(0.0f, inventoryExamineContentHeight - visibleHeight);

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
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
        const Rectangle clipArea = getNotebookContentBounds();

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
            { dialog.x + xOffset, clipArea.y + contentY - inventoryExamineScrollY },
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

        if (item->weightLb > 0.0f)
        {
            char weightLine[64];
            std::snprintf(weightLine, sizeof(weightLine), "Weight: %.2f lb", item->weightLb);
            contentY += lineHeight * 0.4f;
            DrawTextEx(
                descriptionFont,
                weightLine,
                { dialog.x + xOffset, clipArea.y + contentY - inventoryExamineScrollY },
                fontSize * 0.85f,
                spacing,
                textColor);
            contentY += lineHeight;
        }

        inventoryExamineContentHeight = contentY;
        EndScissorMode();
    }

    void Location::drawNarrativeScrollbar() const
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const float maxScroll = std::max(0.0f, narrativeContentHeight - visibleHeight);

        const Rectangle scrollTrack = {
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
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

        const Rectangle clipArea = getNotebookContentBounds();

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
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (isNarrativeSketchLine(line))
            {
                textOffsetY += getNarrativeSketchHeight(narrativeSketchPathFromLine(line));
                continue;
            }

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            const Color lineColor = narrativeLineColor(line);
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, true, narrativeScrollY, lineColor);
        }

        drawNarrativeSketches();

        EndScissorMode();
    }

    void Location::layoutWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY, bool draw, float scrollY, Color lineColor) const
    {
        int length = TextLength(text);

        float textOffsetX = 0.0f;
        float scaleFactor = paragraphFontSize / (float)font.baseSize;
        float lineHeight = (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
        const float wrapWidth = getNarrativeWrapWidth();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
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

                    const float drawY = content.y + textOffsetY - scrollY;
                    const bool visible = (textOffsetY + lineHeight > scrollY) &&
                                         (textOffsetY < scrollY + visibleHeight);

                    if (draw && visible && (codepoint != ' ') && (codepoint != '\t'))
                    {
                        DrawTextCodepoint(
                            font,
                            codepoint,
                            Vector2{ dialog.x + xOffset + textOffsetX, drawY },
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
