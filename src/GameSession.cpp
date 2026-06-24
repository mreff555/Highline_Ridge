#include <GameSession.h>
#include <ImageCompression.h>
#include <PlayerStats.h>
#include <RaylibCompat.h>
#include <raylib.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace highline_ridge
{

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

    struct ResolvedActorSpeakTarget
    {
        std::string phaseId;
        std::string randomLineId;
    };

    bool hasSaloonBartenderProgress(
        const WorldState& worldState,
        const ConversationManager& conversationMgr)
    {
        return worldState.storyFlags.count("saloon_interior:chose_water") > 0
            || worldState.storyFlags.count("saloon_interior:chose_usual") > 0
            || worldState.storyFlags.count("saloon_interior:room_tab") > 0
            || conversationMgr.isPhaseComplete("bartender");
    }

    bool hasSaloonBurgundyProgress(
        const WorldState& worldState,
        const ConversationManager& conversationMgr)
    {
        return worldState.storyFlags.count("saloon_interior:room_tab") > 0
            || worldState.storyFlags.count("saloon_interior:blue_woman_hired") > 0
            || conversationMgr.isPhaseComplete("burgundy_woman");
    }

    bool hasSaloonConversationProgress(
        const WorldState& worldState,
        const ConversationManager& conversationMgr)
    {
        if (worldState.currentSceneId != "saloon_interior")
            return false;

        return hasSaloonBartenderProgress(worldState, conversationMgr)
            || hasSaloonBurgundyProgress(worldState, conversationMgr);
    }

    bool isActorIntroOnlySpeakPhase(
        const std::string& actorId,
        const std::string& phaseId,
        const WorldState& worldState,
        const ConversationManager& conversationMgr)
    {
        if (actorId == "bartender" && phaseId == "bartender")
            return !hasSaloonBartenderProgress(worldState, conversationMgr);
        if (actorId == "burgundy_woman" && phaseId == "burgundy_woman")
            return !hasSaloonBurgundyProgress(worldState, conversationMgr);
        return false;
    }

    ResolvedActorSpeakTarget resolveActorSpeakTarget(
        const ConversationManager& conversationMgr,
        const SceneSpeakConfig& config,
        const std::string& actorId,
        const std::set<std::string>& storyFlags)
    {
        ResolvedActorSpeakTarget resolved;
        resolved.phaseId = conversationMgr.bestStartablePhaseIdForActor(
            config,
            actorId,
            storyFlags);

        if (!resolved.phaseId.empty())
            return resolved;

        for (const ConversationPhase& phase : config.phases)
        {
            if (phase.type != ConversationPhaseType::Random)
                continue;
            if (!conversationMgr.isPhaseRequirementMet(phase, storyFlags))
                continue;

            for (const RandomConversationLine& line : phase.lines)
            {
                if (conversationMgr.lineActorId(line) != actorId)
                    continue;
                if (!conversationMgr.canPickRandomLine(line))
                    continue;

                resolved.phaseId = phase.id;
                resolved.randomLineId = line.id;
                return resolved;
            }
        }

        return resolved;
    }

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

    GameSession::GameSession(
        const LocationStruct& locationStruct,
        Vector2 screenSize,
        SceneDatabase& sceneDatabase,
        const ItemDatabase& itemDatabase,
        const ItemCombinationDatabase& itemCombinationDatabase,
        const MilestoneDatabase& milestoneDatabase,
        AudioManager& audioManager,
        GameConfig& gameConfig,
        const UiBackdrop& uiBackdrop,
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
      uiBackdrop(uiBackdrop),
      pauseMenu((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
      saveLoadMenu((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
      dropConfirmMgr((int)screenSize.x, (int)screenSize.y, locationStruct.uiFont),
      gameConfigPath(configPath),
      isUnderConstruction(locationStruct.isUnderConstruction),
      baseDescription(locationStruct.locationDescription),
      
      examineDetails(locationStruct.examineDetails),
      examineFlag(locationStruct.examineFlag),
      examineLucidityDelta(locationStruct.examineLucidityDelta),
      examineLucidityOncePerDay(locationStruct.examineLucidityOncePerDay),
      speakDetails(locationStruct.speakDetails),
      useDetails(locationStruct.useDetails),
      useHealthDelta(locationStruct.useHealthDelta),
      useEnergyDelta(locationStruct.useEnergyDelta),
      useRepeatStatus(locationStruct.useRepeatStatus),
      useRequiresExamine(locationStruct.useRequiresExamine),
      useAdvancesDay(locationStruct.useAdvancesDay),
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
      buttonMgr(buttonBox, locationStruct.uiFont, locationStruct.boldFont),
      progressionService(milestoneMgr),
      sceneController(sceneDatabase, audioManager)
    {
        inventoryMgr.setFont(locationStruct.uiFont);
        takeMgr.setFont(locationStruct.uiFont);
        interactionMgr.setFont(locationStruct.uiFont);
        speakTargetMgr.setFont(locationStruct.uiFont);
        const std::string& assetRoot = sceneDatabase.getAssetRoot();
        const std::string fallbackRoot = (assetRoot == ".") ? ".." : ".";
        inventoryMgr.setAssetRoots(assetRoot, fallbackRoot);
        inventoryMgr.setItemDatabase(&itemDatabase);
        inventoryMgr.setItemCombinationDatabase(&itemCombinationDatabase);
        takeMgr.setAssetRoots(assetRoot, fallbackRoot);
        buttonMgr.setUiBackdrop(&uiBackdrop);
        inventoryMgr.setUiBackdrop(&uiBackdrop);
        takeMgr.setUiBackdrop(&uiBackdrop);
        interactionMgr.setUiBackdrop(&uiBackdrop);
        speakTargetMgr.setUiBackdrop(&uiBackdrop);
        pauseMenu.setUiBackdrop(&uiBackdrop);
        saveLoadMenu.setUiBackdrop(&uiBackdrop);
        dropConfirmMgr.setUiBackdrop(&uiBackdrop);
        if (!inventoryMgr.ensureIconAssetsLoaded())
            TraceLog(LOG_WARNING, "Some inventory icons failed to load at startup");
        worldState.currentSceneId = sceneId;
        worldState.narrativeText = locationStruct.locationDescription;
        trimNarrativeBuffer();
        pauseMenu.setAudioManager(&audioManager);
        pauseMenu.setGameConfig(&gameConfig);
        pauseMenu.setConfigPath(gameConfigPath);
        pauseMenu.setOnDisplaySettingsChanged([this]() { applyDisplayConfig(); });
        pauseMenu.setOnInputSettingsChanged([this]() { applyInputConfig(); });
        applyInputConfig();
        narrativeNotebook.setFonts(descriptionFont, boldFont);
        narrativeNotebook.getNarrativeText() = locationStruct.locationDescription;
        narrativeNotebook.setAssetRoot(assetRoot);
        conversationMgr.setProgressionService(&progressionService);
        saveLoadMenu.setSaveGameService(&saveGameService);
        conversationMgr.onEnterScene(worldState.currentSceneId, sceneDatabase.getSpeakConfig(worldState.currentSceneId));
        evaluateMilestones();
        sceneController.getActiveScene().loadFromStruct(sceneId, locationStruct);
        syncFromActiveScene();
        updateInventoryLayout();
        updateActionAvailability();
    }

    void GameSession::relayoutForScreenSize(int width, int height)
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


        narrativeNotebook.invalidateLayout();
        inventoryMgr.reloadItemIconsIfNeeded();
        updateInventoryLayout();
    }

    void GameSession::applyDisplayConfig()
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

    void GameSession::applyInputConfig()
    {
        buttonMgr.setClickHoldDuration(gameConfig.input.clickHoldSeconds);
    }

    GameSession::~GameSession()
    {
        if (ownsLocationImage && locationImage.id != 0)
            UnloadTexture(locationImage);
    }

    Texture2D GameSession::getImage() const
    {
        return locationImage;
    }
    char* GameSession::getDescription() const
    {
        return (char*)narrativeNotebook.getNarrativeText().c_str();
    }
    const Font GameSession::getDescriptionFont() const
    {
        return descriptionFont;
    }
    bool GameSession::isUp() const
    {
        return up;
    }
    bool GameSession::isDown() const
    {
        return down;
    }
    bool GameSession::isForward() const
    {
        return forward;
    }
    bool GameSession::isBackward() const
    {
        return backward;
    }
    bool GameSession::isLeft() const
    {
        return left;
    }
    bool GameSession::isRight() const
    {
        return right;
    }



    Rectangle GameSession::getMainImageBounds() const
    {
        return { 0.0f, 0.0f, (float)screenWidth * 0.5f, (float)screenHeight };
    }



















    void GameSession::drawMainImage() const
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

    bool GameSession::isSidePanelOpen() const
    {
        return uiCoordinator.isSidePanelOpen(inventoryMgr, takeMgr, interactionMgr, speakTargetMgr);
    }

    std::string GameSession::takenItemKey(const std::string& itemId) const
    {
        return worldState.currentSceneId + ":" + itemId;
    }

    std::vector<TakeableItemDef> GameSession::getDroppedItemsInCurrentScene() const
    {
        const std::map<std::string, std::vector<TakeableItemDef>>::const_iterator it =
            worldState.droppedItemsByScene.find(worldState.currentSceneId);
        if (it == worldState.droppedItemsByScene.end())
            return std::vector<TakeableItemDef>();

        return it->second;
    }

    bool GameSession::isDroppedItemInCurrentScene(const std::string& itemId) const
    {
        for (const TakeableItemDef& item : getDroppedItemsInCurrentScene())
        {
            if (item.id == itemId)
                return true;
        }

        return false;
    }

    void GameSession::removeDroppedItem(const std::string& sceneId, const std::string& itemId)
    {
        std::map<std::string, std::vector<TakeableItemDef>>::iterator sceneIt =
            worldState.droppedItemsByScene.find(sceneId);
        if (sceneIt == worldState.droppedItemsByScene.end())
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
            worldState.droppedItemsByScene.erase(sceneIt);
    }

    TakeableItemDef GameSession::takeableFromInventory(const InventoryItem& item) const
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

    void GameSession::dropInventoryItem(const std::string& itemId)
    {
        const InventoryItem* item = inventoryMgr.getItemById(itemId);
        if (item == nullptr)
            return;

        removeDroppedItem(worldState.currentSceneId, itemId);
        worldState.droppedItemsByScene[worldState.currentSceneId].push_back(takeableFromInventory(*item));

        if (!inventoryMgr.removeItem(itemId))
            return;

        refreshTakeItems();
        updateActionAvailability();
    }

    void GameSession::handleInventoryDropInput()
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
        uiCoordinator.openDropConfirm();
    }

    std::string GameSession::buildExamineDetailsWithDroppedItems() const
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

    std::vector<TakeableItemDef> GameSession::getAvailableTakeables() const
    {
        std::vector<TakeableItemDef> available;
        const std::vector<TakeableItemDef>& takeables = sceneDatabase.getTakeables(worldState.currentSceneId);

        for (const TakeableItemDef& item : takeables)
        {
            if (worldState.takenItemKeys.count(takenItemKey(item.id)) > 0)
                continue;

            if (inventoryMgr.hasItem(item.id))
                continue;

            if (item.requiresExamine && !hasExaminedScene(worldState.currentSceneId))
                continue;

            if (!item.requiresStoryFlag.empty()
                && worldState.storyFlags.count(item.requiresStoryFlag) == 0)
                continue;

            available.push_back(item);
        }

        for (const TakeableItemDef& item : getDroppedItemsInCurrentScene())
        {
            if (inventoryMgr.hasItem(item.id))
                continue;

            if (item.requiresExamine && !hasExaminedScene(worldState.currentSceneId))
                continue;

            available.push_back(item);
        }

        return available;
    }

    bool GameSession::canTakeInCurrentScene() const
    {
        return !getAvailableTakeables().empty();
    }

    InventoryItem GameSession::buildInventoryItem(
        const std::string& defId,
        const ItemDefOverrides& overrides) const
    {
        std::vector<std::string> flags(worldState.storyFlags.begin(), worldState.storyFlags.end());
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

    void GameSession::playItemExamineAudio(const InventoryItem& item)
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

    void GameSession::clearItemExamineAudio()
    {
        audioManager.clearItemExamineAudio();
    }

    bool GameSession::canTakeFromExaminedItem() const
    {
        return inventoryMgr.isOpen()
            && inventoryMgr.isExaminingItem()
            && inventoryMgr.canExtractFromExaminedItem(itemDatabase);
    }

    bool GameSession::canUseSelectedInventoryItem() const
    {
        if (!inventoryMgr.isOpen() || !inventoryMgr.hasSelectedItem())
        {
            return false;
        }

        const InventoryItem* item = inventoryMgr.getSelectedItem();
        if (item == nullptr)
            return false;

        const ItemDef* def = itemDatabase.getDef(item->id);
        if (def == nullptr || def->useRevealFlag.empty())
            return false;

        if (hasItemFlag(item->instance.activeFlags, def->useRevealFlag))
            return false;

        if (!def->useRequiresFlag.empty()
            && !hasItemFlag(item->instance.activeFlags, def->useRequiresFlag)
            && !inventoryMgr.isExaminingItem())
        {
            return false;
        }

        return true;
    }

    void GameSession::useSelectedInventoryItem()
    {
        if (!canUseSelectedInventoryItem())
            return;

        const std::string itemId = inventoryMgr.getSelectedItemId();
        const ItemDef* def = itemDatabase.getDef(itemId);
        if (def == nullptr)
            return;

        const bool alreadyExamining = inventoryMgr.isExaminingItem();

        inventoryMgr.applyExamineRevealFlag(itemId, def->useRevealFlag);

        if (!alreadyExamining)
            inventoryMgr.examineSelectedItem();

        const InventoryItem* examinedItem = inventoryMgr.getSelectedItem();
        if (examinedItem != nullptr)
            playItemExamineAudio(*examinedItem);

        if (!def->useNarrative.empty())
            appendNarrativeSection("Using:", def->useNarrative);

        narrativeNotebook.resetInventoryExamineScroll();
        updateActionAvailability();
        worldState.recordAction();
    }

    void GameSession::takeFromExaminedItem()
    {
        InventoryItem extracted;
        if (!inventoryMgr.extractFromExaminedItem(itemDatabase, extracted))
            return;

        if (extracted.id.empty())
            return;

        inventoryMgr.addItem(extracted);
        evaluateMilestones();
    }

    void GameSession::addTakenItemToInventory(const TakeableItemDef& taken)
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

    void GameSession::refreshTakeItems()
    {
        takeMgr.setAvailableItems(getAvailableTakeables());
    }

    void GameSession::processPendingTakes()
    {
        if (takeMgr.consumePendingTakeAll())
        {
            const std::vector<TakeableItemDef> takenItems = takeMgr.takeAll();
            for (const TakeableItemDef& taken : takenItems)
            {
                if (isDroppedItemInCurrentScene(taken.id))
                    removeDroppedItem(worldState.currentSceneId, taken.id);
                else
                    worldState.takenItemKeys.insert(takenItemKey(taken.id));

                addTakenItemToInventory(taken);
            }
            refreshTakeItems();
            if (takeMgr.isEmpty())
            {
                closeAllUiPanels();
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
            removeDroppedItem(worldState.currentSceneId, taken.id);
        else
            worldState.takenItemKeys.insert(takenItemKey(taken.id));

        addTakenItemToInventory(taken);
        refreshTakeItems();
        if (takeMgr.isEmpty())
        {
            closeAllUiPanels();
            updateInventoryLayout();
        }
        updateActionAvailability();
    }

    Rectangle GameSession::getInventoryPanelBounds() const
    {
        const Rectangle dialog = getDialogBounds();
        return {
            textBox.x,
            dialog.y + dialog.height,
            textBox.width,
            fullDialogHeight * kSidePanelHeightShare
        };
    }

    void GameSession::updateInventoryLayout()
    {
        if (inventoryMgr.isOpen() || takeMgr.isOpen() || interactionMgr.isOpen() || speakTargetMgr.isOpen())
        {
            const Rectangle panelBounds = getInventoryPanelBounds();
            if (inventoryMgr.isOpen())
                inventoryMgr.setPanelBounds(panelBounds);
            if (takeMgr.isOpen())
                takeMgr.setPanelBounds(panelBounds);
            if (interactionMgr.isOpen())
                interactionMgr.setPanelBounds(panelBounds);
            if (speakTargetMgr.isOpen())
                speakTargetMgr.setPanelBounds(panelBounds);
        }
    }













    bool GameSession::hasExaminedScene(const std::string& sceneId) const
    {
        return worldState.sceneVisits.examinedSceneIds.count(sceneId) > 0;
    }

    std::string GameSession::interactionKey(const std::string& interactionId) const
    {
        return worldState.currentSceneId + ":" + interactionId;
    }

    std::vector<SceneInteractionDef> GameSession::getAvailableInteractions() const
    {
        std::vector<SceneInteractionDef> available;
        const std::vector<SceneInteractionDef>& interactions =
            sceneDatabase.getInteractions(worldState.currentSceneId);

        for (const SceneInteractionDef& interaction : interactions)
        {
            if (interaction.requiresExamine && !hasExaminedScene(worldState.currentSceneId))
                continue;

            if (!interaction.repeat &&
                worldState.usedInteractionKeys.count(interactionKey(interaction.id)) > 0)
            {
                continue;
            }

            available.push_back(interaction);
        }

        return available;
    }

    bool GameSession::canUseInCurrentScene() const
    {
        if (!getAvailableInteractions().empty())
            return true;

        if (useDetails.empty() && useExit.empty())
            return false;

        if (useRequiresExamine && !hasExaminedScene(worldState.currentSceneId))
            return false;

        if (useRepeatStatus)
            return !worldState.sceneVisits.hasUsedInCurrentScene;

        return worldState.sceneVisits.usedSceneIds.count(worldState.currentSceneId) == 0;
    }

























    void GameSession::evaluateMilestones()
    {
        progressionService.evaluate(
            worldState.storyFlags,
            conversationMgr,
            worldState.currentSceneId,
            [this](const std::string& itemId)
            {
                return inventoryMgr.hasItem(itemId);
            });
    }

    void GameSession::appendExamineDetails()
    {
        const std::string details = buildExamineDetailsWithDroppedItems();
        if (details.empty())
            return;

        appendNarrativeSection("Examining:", details);
        if (!examineFlag.empty())
            worldState.storyFlags.insert(examineFlag);

        if (examineLucidityDelta != 0.0f)
        {
            StatusEffect examineEffect;
            examineEffect.key = worldState.currentSceneId + ":examine:lucidity";
            if (examineLucidityOncePerDay)
                examineEffect.key += ":day:" + std::to_string(worldState.day);
            examineEffect.lucidity = examineLucidityDelta;
            tryApplyStatusEffect(examineEffect, false);
        }

        worldState.sceneVisits.examinedSceneIds.insert(worldState.currentSceneId);
        evaluateMilestones();
        updateActionAvailability();
        worldState.recordAction();
    }

    void GameSession::appendSpeakDetails()
    {
        if (speakDetails.empty() || worldState.sceneVisits.hasSpokenInCurrentScene)
            return;

        appendNarrativeSection("Speaking:", speakDetails);
        worldState.sceneVisits.hasSpokenInCurrentScene = true;
        updateActionAvailability();
        worldState.recordAction();
    }









    void GameSession::applyStatusEffects(const std::vector<StatusEffect>& effects)
    {
        bool statsChanged = false;

        for (const StatusEffect& effect : effects)
        {
            if (!effect.hasDelta())
                continue;

            if (tryApplyStatusEffect(effect, effect.repeat))
            {
                statsChanged = true;

                if (effect.onZeroLucidity == "restart_cabin" && worldState.playerStats.lucidity <= 0.0f)
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

    void GameSession::grantConversationItem(const GrantedInventoryItemDef& granted)
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

    void GameSession::playDialogAudio(const SpeakResult& result)
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

    void GameSession::playInteractionTts(const SceneInteractionDef& interaction, bool includeAfter)
    {
        if (!interaction.tts || !gameConfig.tts.enabled)
            return;

        const bool useVariant =
            !interaction.ttsVariantFlag.empty()
            && worldState.storyFlags.count(interaction.ttsVariantFlag) > 0
            && !interaction.ttsVariantAudio.empty();

        std::vector<std::string> audioPaths;
        if (useVariant)
            audioPaths.push_back(interaction.ttsVariantAudio);
        else if (!interaction.ttsAudio.empty())
            audioPaths.push_back(interaction.ttsAudio);

        if (includeAfter && interaction.ttsAfter && !interaction.ttsAfterAudio.empty())
            audioPaths.push_back(interaction.ttsAfterAudio);

        if (audioPaths.empty())
            return;

        if (audioManager.playDialogAssetSequence(audioPaths))
            return;

        TraceLog(
            LOG_WARNING,
            "Missing bundled TTS audio (run with --refresh-voices=API_KEY)");
    }

    void GameSession::processSpeakResult(const SpeakResult& result)
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
            refreshSceneImage();
            if (!result.spokenActorId.empty())
                worldState.markActorKnown(result.spokenActorId);
            updateActionAvailability();
            return;
        }

        applyStatusEffects(result.statusEffects);
        grantConversationItem(result.grantItem);
        playDialogAudio(result);
        evaluateMilestones();
        refreshSceneImage();

        if (!result.spokenActorId.empty())
            worldState.markActorKnown(result.spokenActorId);
    }

    void GameSession::resolveDialogChoice(const std::string& choiceId)
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

        if (selectedChoice != nullptr && !selectedChoice->isAvailable(worldState.playerStats.walletCash))
            return;

        const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(worldState.currentSceneId);
        SpeakResult result = conversationMgr.resolveChoice(speakConfig, choiceId);
        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            result = conversationMgr.resolveChoiceFromConfig(speakConfig, choiceId);

        if (result.action == SpeakResult::Action::None && result.narrative.empty())
            return;

        worldState.recordAction();

        const std::string responseText = result.narrative;
        const std::vector<StatusEffect> effects = result.statusEffects;

        narrativeNotebook.getChoiceHitAreas().clear();

        if (!selectedLineText.empty())
            worldState.committedPlayerDialogLines.insert(selectedLineText);

        stripDialogChoiceLinesFromNarrative(choicesToStrip, selectedLineText);

        if (!selectedSketchPath.empty())
            appendNarrativeSketch(selectedSketchPath);

        if (!responseText.empty())
        {
            narrativeNotebook.getNarrativeText() += "\n\n";
            narrativeNotebook.getNarrativeText() += responseText;
            trimNarrativeBuffer();
            narrativeNotebook.invalidateLayout();
        }

        if (result.action == SpeakResult::Action::ShowChoices)
        {
            applyStatusEffects(effects);
            playDialogAudio(result);
            const std::vector<ConversationChoiceDef>& pending = conversationMgr.getPendingChoices();
            appendChoiceLinesToNarrative(
                !pending.empty() ? pending : result.choices,
                selectedLineText);
            if (!result.grantStoryFlag.empty())
                applyGrantedStoryFlag(result.grantStoryFlag);
            updateActionAvailability();
            evaluateMilestones();
            refreshSceneImage();
            return;
        }

        applyStatusEffects(effects);
        grantConversationItem(result.grantItem);
        playDialogAudio(result);

        if (!selectedLineText.empty())
            scrollNarrativeToLine(selectedLineText, true);
        else if (!responseText.empty())
        {
            rebuildNarrativeLayout();
            std::istringstream responseStream(responseText);
            std::string firstLine;
            if (std::getline(responseStream, firstLine) && !firstLine.empty())
                scrollNarrativeToLine(firstLine, true);
        }

        if (!result.grantStoryFlag.empty())
            applyGrantedStoryFlag(result.grantStoryFlag);

        if (!result.overlaySequence.empty())
            triggerOverlaySequence(result.overlaySequence);

        updateActionAvailability();
        evaluateMilestones();
        refreshSceneImage();

        if (!result.startPhaseId.empty())
        {
            SpeakResult chained = conversationMgr.startScriptedPhase(
                speakConfig,
                result.startPhaseId,
                worldState.storyFlags,
                result.skipIntroOnStartPhase);
            processSpeakResult(chained);
            updateActionAvailability();
        }

        if (!result.spokenActorId.empty())
            worldState.markActorKnown(result.spokenActorId);

        if (conversationMgr.isPhaseComplete("blue_woman"))
            worldState.storyFlags.insert("saloon_balcony:blue_woman_done");
    }

    void GameSession::resolveCombatEncounter(const std::string& encounterId)
    {
        if (!conversationMgr.isAwaitingChoice() || !conversationMgr.isCombatAttackAllowed())
            return;

        if (conversationMgr.getCombatEncounterId() != encounterId)
            return;

        const std::vector<ConversationChoiceDef> choicesToStrip = conversationMgr.getPendingChoices();
        narrativeNotebook.getChoiceHitAreas().clear();
        stripDialogChoiceLinesFromNarrative(choicesToStrip);

        std::string responseText;
        StatusEffect effect;

        if (encounterId == "drunk_patron")
        {
            if (worldState.playerStats.resolve > 65.0f)
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
                effect.actor = "drunk_patron";
                effect.actorOpinion = -3;
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
                effect.actor = "drunk_patron";
                effect.actorOpinion = -3;
            }
        }
        else
        {
            return;
        }

        conversationMgr.clearPendingEncounter();

        if (!responseText.empty())
        {
            narrativeNotebook.getNarrativeText() += "\n\n";
            narrativeNotebook.getNarrativeText() += responseText;
            trimNarrativeBuffer();
            narrativeNotebook.invalidateLayout();
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

    void GameSession::handleAttack()
    {
        if (!conversationMgr.isCombatAttackAllowed())
            return;

        resolveCombatEncounter(conversationMgr.getCombatEncounterId());
    }

    void GameSession::syncKnownActorsFromProgress()
    {
        if (worldState.currentSceneId != "saloon_interior")
            return;

        if (hasSaloonBartenderProgress(worldState, conversationMgr))
            worldState.markActorKnown("bartender");
        if (hasSaloonBurgundyProgress(worldState, conversationMgr))
            worldState.markActorKnown("burgundy_woman");
    }

    bool GameSession::shouldOpenSpeakPicker() const
    {
        const SceneSpeakConfig& config = sceneDatabase.getSpeakConfig(worldState.currentSceneId);
        const std::vector<SceneActorDef> actors = sceneDatabase.getSceneActors(worldState.currentSceneId);
        if (actors.size() < 2 || !config.hasPhases())
            return false;

        for (const SceneActorDef& actor : actors)
        {
            if (worldState.isActorKnown(actor.id))
                return true;
        }

        return hasSaloonConversationProgress(worldState, conversationMgr);
    }

    void GameSession::handleSpeak()
    {
        syncKnownActorsFromProgress();

        const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(worldState.currentSceneId);
        if (speakConfig.hasPhases())
        {
            if (shouldOpenSpeakPicker())
            {
                if (speakTargetMgr.isOpen())
                    closeAllUiPanels();
                else
                {
                    refreshSpeakTargets();
                    openUiMode(UiMode::Speak);
                    updateInventoryLayout();
                }
                updateActionAvailability();
                return;
            }

            SpeakResult result = conversationMgr.handleSpeak(
                worldState.currentSceneId,
                speakConfig,
                worldState.storyFlags);
            if (result.action != SpeakResult::Action::None
                || !result.narrative.empty()
                || !result.sketchPath.empty())
            {
                worldState.recordAction();
            }
            processSpeakResult(result);
            return;
        }

        appendSpeakDetails();
    }

    void GameSession::applyLucidityCollapseRestart()
    {
        LocationStruct cabinLocation;
        std::string startSceneId;
        if (!sceneDatabase.loadStartScene(cabinLocation, startSceneId))
        {
            TraceLog(LOG_ERROR, "Failed to restart at cabin after lucidity collapse");
            return;
        }

        sceneController.getActiveScene().unloadOwnedImage();
        worldState.previousSceneId.clear();
        worldState.currentSceneId = startSceneId;
        worldState.sceneVisits.examinedSceneIds.clear();
        worldState.sceneVisits.usedSceneIds.clear();
        worldState.takenItemKeys.clear();
        worldState.usedInteractionKeys.clear();
        worldState.storyFlags.clear();
        worldState.committedPlayerDialogLines.clear();
        worldState.droppedItemsByScene.clear();
        worldState.knownActorIds.clear();
        closeAllUiPanels();
        applyLocationStruct(cabinLocation);

        narrativeNotebook.getNarrativeText() += "\n\n";
        narrativeNotebook.getNarrativeText() += kWakeOnFloorPrefix;
        narrativeNotebook.getNarrativeText() += "\n\n";
        narrativeNotebook.getNarrativeText() += baseDescription;
        worldState.playerStats.health = 90.0f;
        worldState.playerStats.energy = 20.0f;
        worldState.playerStats.resolve = 50.0f;
        worldState.playerStats.lucidity = 30.0f;
        worldState.playerStats.charisma = 50.0f;
        worldState.playerStats.walletCash = 20.0f;
        narrativeNotebook.resetNarrativeScroll();
        narrativeNotebook.invalidateLayout();
        trimNarrativeBuffer();
        worldState.advanceDay();
        updateActionAvailability();
    }



    bool GameSession::tryApplyStatusEffect(const StatusEffect& effect, bool allowRepeat)
    {
        if (!effect.hasDelta())
            return false;

        if (!allowRepeat && !effect.key.empty()
            && worldState.playerStats.consumedStatusActions.count(effect.key) > 0)
            return false;

        bool applied = false;
        if (effect.hasPlayerDelta())
            applied = worldState.playerStats.apply(effect, allowRepeat);

        if (effect.hasActorOpinionDelta())
        {
            worldState.applyActorOpinionDelta(effect.actor, effect.actorOpinion);
            applied = true;
        }

        if (applied && !allowRepeat && !effect.key.empty()
            && worldState.playerStats.consumedStatusActions.count(effect.key) == 0)
            worldState.playerStats.consumedStatusActions.insert(effect.key);

        return applied;
    }

    void GameSession::transitionToScene(const std::string& nextSceneId)
    {
        if (!sceneController.transitionToScene(nextSceneId, worldState, takeMgr, interactionMgr))
            return;

        closeAllUiPanels();
        syncFromActiveScene();
        narrativeNotebook.getNarrativeText() = worldState.narrativeText;
        conversationMgr.onEnterScene(worldState.currentSceneId, sceneDatabase.getSpeakConfig(worldState.currentSceneId));
        narrativeNotebook.resetNarrativeScroll();
        narrativeNotebook.invalidateLayout();
        trimNarrativeBuffer();
        evaluateMilestones();
        updateActionAvailability();
    }

    std::string GameSession::formatNewspaperDate(int day)
    {
        int year = 1891;
        int month = 4;
        int date = 3 + std::max(0, day - 1);

        auto daysInMonth = [](int monthIndex, int yearValue)
        {
            static const int kDaysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            int days = kDaysPerMonth[(size_t)(monthIndex - 1)];
            if (monthIndex == 2 && yearValue % 4 == 0 && (yearValue % 100 != 0 || yearValue % 400 == 0))
                ++days;
            return days;
        };

        static const char* kMonthNames[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };

        while (date > daysInMonth(month, year))
        {
            date -= daysInMonth(month, year);
            ++month;
            if (month > 12)
            {
                month = 1;
                ++year;
            }
        }

        return std::string(kMonthNames[(size_t)(month - 1)]) + " " + std::to_string(date)
            + ", " + std::to_string(year);
    }

    void GameSession::applyBedroomReadNewspaper(const SceneInteractionDef& interaction)
    {
        const std::string dateText = formatNewspaperDate(worldState.day);
        const std::string details =
            "You unfold the newspaper on the chair and smooth the creased mining briefs flat with your thumb. "
            "The masthead is printed in sober type above the fold:\n\n"
            + dateText + ".\n\n"
            "The date sits there without apology, as though the ridge and the valley below agreed on the same calendar "
            "for once. You read a line about ore shipments and weather over Graysmill, then fold the paper back the "
            "way you found it.";

        appendNarrativeSection("Using:", details);
        playInteractionTts(interaction);

        StatusEffect useEffect;
        useEffect.key = interactionKey(interaction.id);
        useEffect.lucidity = interaction.useLucidityDelta;
        tryApplyStatusEffect(useEffect, interaction.repeat);

        closeAllUiPanels();
        updateInventoryLayout();
        worldState.recordAction();
        updateActionAvailability();
    }

    void GameSession::finishBedroomSleep(const SceneInteractionDef& interaction, bool blueWomanHired)
    {
        const std::string wakeDetails =
            "When you wake, gray light sits at the window and the bourbon bottle throws a darker shadow on the "
            "dresser. Your mouth tastes of smoke and iron. The newspaper has not moved. The room remembers you, "
            "but only barely.";
        appendNarrativeSection("Using:", wakeDetails);
        playInteractionTts(interaction, true);

        StatusEffect sleepEffect;
        sleepEffect.key = interactionKey(interaction.id);
        sleepEffect.health = interaction.useHealthDelta;
        sleepEffect.energy = interaction.useEnergyDelta;
        sleepEffect.lucidity = interaction.useLucidityDelta;
        tryApplyStatusEffect(sleepEffect, interaction.repeat);

        if (blueWomanHired)
        {
            StatusEffect hiredEffect;
            hiredEffect.key = interactionKey("sleep_bed:blue_woman");
            hiredEffect.energy = 10.0f;
            hiredEffect.health = 5.0f;
            hiredEffect.lucidity = 10.0f;
            hiredEffect.charisma = 2.0f;
            tryApplyStatusEffect(hiredEffect, interaction.repeat);
        }

        worldState.advanceDay();
        updateActionAvailability();
    }

    void GameSession::applyBedroomSleep(const SceneInteractionDef& interaction)
    {
        std::string details =
            "You lie down in the iron bed and begin to collect your thoughts while staring at the cracked plaster "
            "ceiling. The cracks branch like slow lightning frozen above you, map lines with no legend. The saloon's "
            "muffled pulse travels through the floorboards - piano, laughter, a glass breaking somewhere far from "
            "your door.";

        const bool blueWomanHired =
            worldState.storyFlags.count("saloon_interior:blue_woman_hired") > 0;
        if (blueWomanHired)
        {
            details +=
                "\n\nJust then the door quietly cracks open. The woman in blue peers in, smiling, one hand still on "
                "the knob as though she might vanish if you looked away too long. She waits for permission to enter.\n\n"
                "You motion for her to come.\n\n"
                "She steps in and closes the door behind her with care, the latch settling soft as a held breath.";
        }

        details +=
            "\n\nSleep takes you the way it takes men who have been awake too long in the wrong country: without "
            "permission and without dreams you trust.";

        appendNarrativeSection("Using:", details);
        playInteractionTts(interaction);

        closeAllUiPanels();
        updateInventoryLayout();
        worldState.recordAction();

        if (!interaction.overlaySequence.empty())
        {
            triggerOverlaySequence(
                interaction.overlaySequence,
                [this, interaction, blueWomanHired]()
                {
                    finishBedroomSleep(interaction, blueWomanHired);
                });
        }
        else
            finishBedroomSleep(interaction, blueWomanHired);

        updateActionAvailability();
    }

    void GameSession::applySceneOverlays()
    {
        overlayMgr.setSceneOverlays(sceneDatabase.getOverlays(worldState.currentSceneId));
    }

    void GameSession::triggerOverlaySequence(
        const std::vector<OverlaySequenceStep>& sequence,
        std::function<void()> onComplete)
    {
        if (sequence.empty())
        {
            if (onComplete)
                onComplete();
            return;
        }

        overlayMgr.startSequence(sequence, std::move(onComplete));
    }

    void GameSession::applyInteraction(const SceneInteractionDef& interaction)
    {
        if (interaction.id == "read_newspaper")
        {
            applyBedroomReadNewspaper(interaction);
            return;
        }

        if (interaction.id == "sleep_bed")
        {
            applyBedroomSleep(interaction);
            return;
        }

        if (!interaction.useDetails.empty())
            appendNarrativeSection("Using:", interaction.useDetails);

        playInteractionTts(interaction);

        StatusEffect useEffect;
        useEffect.key = interactionKey(interaction.id);
        if (interaction.oncePerDay)
            useEffect.key += ":day:" + std::to_string(worldState.day);
        useEffect.health = interaction.useHealthDelta;
        useEffect.energy = interaction.useEnergyDelta;
        useEffect.resolve = interaction.useResolveDelta;
        useEffect.lucidity = interaction.useLucidityDelta;
        useEffect.charisma = interaction.useCharismaDelta;
        const bool allowStatusRepeat = interaction.repeat && !interaction.oncePerDay;
        tryApplyStatusEffect(useEffect, allowStatusRepeat);

        if (!interaction.repeat)
            worldState.usedInteractionKeys.insert(interactionKey(interaction.id));

        closeAllUiPanels();
        updateInventoryLayout();

        if (interaction.advancesDay)
            worldState.advanceDay();

        worldState.recordAction();

        if (!interaction.overlaySequence.empty())
            triggerOverlaySequence(interaction.overlaySequence);

        if (!interaction.exitSceneId.empty())
            transitionToScene(interaction.exitSceneId);
        else
            updateActionAvailability();
    }

    void GameSession::applyDirectUse()
    {
        if (!canUseInCurrentScene())
            return;

        if (!useDetails.empty())
            appendNarrativeSection("Using:", useDetails);

        StatusEffect useEffect;
        useEffect.key = worldState.currentSceneId + ":use";
        useEffect.health = useHealthDelta;
        useEffect.energy = useEnergyDelta;
        tryApplyStatusEffect(useEffect, useRepeatStatus);
        worldState.sceneVisits.hasUsedInCurrentScene = true;
        if (!useRepeatStatus)
            worldState.sceneVisits.usedSceneIds.insert(worldState.currentSceneId);

        if (useAdvancesDay)
            worldState.advanceDay();

        worldState.recordAction();

        const std::string exitSceneId = useExit;
        if (!exitSceneId.empty())
            transitionToScene(exitSceneId);
        else
            updateActionAvailability();
    }

    void GameSession::refreshInteractions()
    {
        interactionMgr.setAvailableInteractions(getAvailableInteractions());
    }

    std::vector<SpeakTargetDef> GameSession::getAvailableSpeakTargets() const
    {
        const SceneSpeakConfig& config = sceneDatabase.getSpeakConfig(worldState.currentSceneId);
        const std::vector<SceneActorDef> actors = sceneDatabase.getSceneActors(worldState.currentSceneId);
        if (actors.size() < 2 || !config.hasPhases())
            return {};

        std::vector<SpeakTargetDef> targets;
        for (const SceneActorDef& actor : actors)
        {
            const ResolvedActorSpeakTarget resolved = resolveActorSpeakTarget(
                conversationMgr,
                config,
                actor.id,
                worldState.storyFlags);

            if (resolved.phaseId.empty() && resolved.randomLineId.empty())
                continue;

            if (!resolved.randomLineId.empty())
            {
                if (!worldState.isActorKnown(actor.id))
                    continue;
            }
            else if (isActorIntroOnlySpeakPhase(
                         actor.id,
                         resolved.phaseId,
                         worldState,
                         conversationMgr))
            {
                continue;
            }

            SpeakTargetDef target;
            target.id = actor.id;
            target.label = actor.name;
            target.phaseId = resolved.phaseId;
            target.randomLineId = resolved.randomLineId;
            targets.push_back(target);
        }

        if (conversationMgr.canWorkTheRoom(config, worldState.storyFlags))
        {
            SpeakTargetDef workTheRoom;
            workTheRoom.id = "__work_the_room__";
            workTheRoom.label = "Work the room";
            workTheRoom.isWorkTheRoom = true;
            targets.push_back(workTheRoom);
        }

        return targets;
    }

    void GameSession::refreshSpeakTargets()
    {
        syncKnownActorsFromProgress();
        speakTargetMgr.setAvailableTargets(getAvailableSpeakTargets());
    }

    void GameSession::processPendingSpeakTargets()
    {
        const SpeakTargetDef target = speakTargetMgr.consumePendingTarget();
        if (target.id.empty())
            return;

        closeAllUiPanels();
        updateInventoryLayout();

        const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(worldState.currentSceneId);
        SpeakResult result;
        if (target.isWorkTheRoom)
        {
            result = conversationMgr.handleSpeakWorkTheRoom(
                worldState.currentSceneId,
                speakConfig,
                worldState.storyFlags);
        }
        else
        {
            const ResolvedActorSpeakTarget resolved = resolveActorSpeakTarget(
                conversationMgr,
                speakConfig,
                target.id,
                worldState.storyFlags);

            result = conversationMgr.handleSpeakTarget(
                worldState.currentSceneId,
                speakConfig,
                worldState.storyFlags,
                resolved.phaseId,
                resolved.randomLineId);
        }

        if (result.action != SpeakResult::Action::None
            || !result.narrative.empty()
            || !result.sketchPath.empty())
        {
            worldState.recordAction();
            processSpeakResult(result);
        }

        updateActionAvailability();
    }

    void GameSession::processPendingInteractions()
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
            return;
        }
    }

    void GameSession::appendUseDetails()
    {
        applyDirectUse();
    }

    void GameSession::updateActionAvailability()
    {
        syncKnownActorsFromProgress();

        MovementStruct movement{};
        ActionStruct actions{};

        if (dropConfirmMgr.isOpen())
        {
            buttonMgr.setAvailability(movement, actions);
            {
            const PlayerStatPercents percents = worldState.playerStats.toPercents();
            buttonMgr.setStatus(percents.health, percents.energy, percents.resolve, percents.lucidity, percents.charisma);
        }
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
        else if (speakTargetMgr.isOpen())
        {
            movement.up = false;
            movement.down = false;
            movement.forward = false;
            movement.left = false;
            movement.right = false;
            movement.backward = false;
            actions.speak = true;
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
                if (canUseSelectedInventoryItem())
                    actions.use = true;
            }
            else if (canUseSelectedInventoryItem())
                actions.use = true;
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
            movement.backward = !worldState.previousSceneId.empty();
        }
        else
        {
            movement.up = up;
            movement.down = down;
            movement.forward = forward;
            movement.backward = backward;
            movement.left = left;
            movement.right = right;

            if (worldState.currentSceneId == "saloon_interior" && !hasExaminedScene(worldState.currentSceneId))
            {
                movement.up = false;
                movement.right = false;
            }

            actions = baseActionFilter;
            const SceneSpeakConfig& speakConfig = sceneDatabase.getSpeakConfig(worldState.currentSceneId);
            if (speakConfig.hasPhases())
            {
                actions.speak = conversationMgr.canSpeak(
                    speakConfig,
                    baseActionFilter.speak,
                    worldState.storyFlags)
                    || shouldOpenSpeakPicker();
            }
            else if (!speakDetails.empty())
                actions.speak = !worldState.sceneVisits.hasSpokenInCurrentScene;
            else
                actions.speak = false;

            actions.use = canUseInCurrentScene();
            actions.take = canTakeInCurrentScene();
        }

        buttonMgr.setAvailability(movement, actions);
        {
            const PlayerStatPercents percents = worldState.playerStats.toPercents();
            buttonMgr.setStatus(percents.health, percents.energy, percents.resolve, percents.lucidity, percents.charisma);
        }
    }








    
    void GameSession::appendBlockedMovementMessage(const std::string& details)
    {
        if (details.empty())
            return;

        narrativeNotebook.getNarrativeText() += "\n\n";
        narrativeNotebook.getNarrativeText() += details;
        trimNarrativeBuffer();
        narrativeNotebook.invalidateLayout();
        scrollNarrativeToLine(details, true);
    }

    bool GameSession::maybeRevealIceHouseInteriorDeparture(const std::string& direction)
    {
        if (worldState.currentSceneId != "ice_house_interior" || direction != "right")
            return false;

        if (!hasExaminedScene(worldState.currentSceneId))
            return false;

        if (worldState.storyFlags.count("ice_house_interior:ranger_badge_revealed") > 0)
            return false;

        const std::string details =
            "You finish examining the room and turn toward the door. As you do, a single ray of "
            "sunshine punches through a gap in the timber frame and strikes the floor where the "
            "hay lies thickest.\n\n"
            "Something answers the light - a brief, hard glint, too deliberate to be ice.\n\n"
            "You kneel. Your fingers close around a small circle of brass half-buried in the "
            "packed earth: a Texas Ranger badge, tarnished at the edges but not forgotten. Whoever "
            "left it here did not mean for it to stay hidden forever.";

        appendNarrativeSection("Examining:", details);
        worldState.storyFlags.insert("ice_house_interior:ranger_badge_revealed");
        evaluateMilestones();
        updateActionAvailability();
        worldState.recordAction();
        return true;
    }

    bool GameSession::maybeRevealCottonwoodMeadowDeparture(const std::string& direction)
    {
        if (worldState.currentSceneId != "cottonwood_meadow" || direction != "backward")
            return false;

        const std::string restKey = interactionKey("rest_by_cottonwood");
        if (worldState.playerStats.consumedStatusActions.count(restKey) == 0)
            return false;

        if (worldState.storyFlags.count("cottonwood_meadow:departure_noted") > 0)
            return false;

        const std::string details =
            "You push yourself up from the cottonwood's shade, brushing grass from your coat. "
            "As you stand, one detail catches your eye that the mountains had distracted you from: "
            "a patch of ground roughly ten feet from the tree's trunk, greener and thicker than "
            "the grass around it, as though something beneath the soil feeds that spot what the "
            "rest of the meadow only wishes for.";

        appendNarrativeSection("Examining:", details);
        worldState.storyFlags.insert("cottonwood_meadow:departure_noted");
        evaluateMilestones();
        updateActionAvailability();
        worldState.recordAction();
        return true;
    }

    void GameSession::tryMove(const std::string& direction)
    {
        if (maybeRevealIceHouseInteriorDeparture(direction))
            return;

        if (maybeRevealCottonwoodMeadowDeparture(direction))
            return;

        std::string blockedDetails;
        if (!sceneController.tryMove(
                direction,
                worldState,
                takeMgr,
                interactionMgr,
                inventoryMgr,
                itemDatabase,
                blockedDetails))
        {
            if (!blockedDetails.empty())
                appendBlockedMovementMessage(blockedDetails);
            return;
        }

        closeAllUiPanels();
        syncFromActiveScene();
        narrativeNotebook.getNarrativeText() = worldState.narrativeText;
        conversationMgr.onEnterScene(worldState.currentSceneId, sceneDatabase.getSpeakConfig(worldState.currentSceneId));
        narrativeNotebook.resetNarrativeScroll();
        narrativeNotebook.invalidateLayout();
        trimNarrativeBuffer();
        evaluateMilestones();
        updateActionAvailability();
        worldState.recordAction();
    }

    void GameSession::refreshSceneImage()
    {
        const SceneData* scene = sceneDatabase.getScene(worldState.currentSceneId);
        if (scene == nullptr)
            return;

        const std::string imagePath = sceneDatabase.resolveSceneImagePath(
            *scene,
            worldState.storyFlags,
            [this](const std::string& phaseId)
            {
                return conversationMgr.isPhaseComplete(phaseId);
            });

        if (!sceneController.getActiveScene().replaceLocationImage(sceneDatabase, imagePath))
            return;

        syncFromActiveScene();
    }

    void GameSession::syncFromActiveScene()
    {
        const LocationStruct& locationStruct = sceneController.getActiveScene().getView();
        locationImage = locationStruct.locationImage;
        ownsLocationImage = false;
        isUnderConstruction = locationStruct.isUnderConstruction;
        baseDescription = locationStruct.locationDescription;
        examineDetails = locationStruct.examineDetails;
        examineFlag = locationStruct.examineFlag;
        examineLucidityDelta = locationStruct.examineLucidityDelta;
        examineLucidityOncePerDay = locationStruct.examineLucidityOncePerDay;
        speakDetails = locationStruct.speakDetails;
        useDetails = locationStruct.useDetails;
        useHealthDelta = locationStruct.useHealthDelta;
        useEnergyDelta = locationStruct.useEnergyDelta;
        useRepeatStatus = locationStruct.useRepeatStatus;
        useRequiresExamine = locationStruct.useRequiresExamine;
        useAdvancesDay = locationStruct.useAdvancesDay;
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
        applySceneOverlays();
    }

    void GameSession::applyLocationStruct(const LocationStruct& locationStruct, const std::string& fromRoom)
    {
        sceneController.getActiveScene().loadFromStruct(worldState.currentSceneId, locationStruct);
        syncFromActiveScene();
        worldState.sceneVisits.resetForNewScene();
        worldState.committedPlayerDialogLines.clear();
        narrativeNotebook.getNarrativeText() = locationStruct.locationDescription;
        worldState.narrativeText = narrativeNotebook.getNarrativeText();
        narrativeNotebook.getChoiceHitAreas().clear();

        conversationMgr.onEnterScene(worldState.currentSceneId, sceneDatabase.getSpeakConfig(worldState.currentSceneId));
        audioManager.onRoomEnter(sceneDatabase.getSceneAudio(worldState.currentSceneId), fromRoom);

        narrativeNotebook.resetNarrativeScroll();
        narrativeNotebook.invalidateLayout();
        trimNarrativeBuffer();
        evaluateMilestones();
        refreshSceneImage();
        updateActionAvailability();
    }

    SavedGameState GameSession::captureSaveState() const
    {
        worldState.narrativeText = narrativeNotebook.getNarrativeText();
        return worldState.snapshot(conversationMgr, milestoneMgr, inventoryMgr);
    }

    bool GameSession::applySaveState(const SavedGameState& state)
    {
        LocationStruct locationStruct;
        if (!sceneDatabase.loadScene(state.sceneId, locationStruct))
            return false;

        const std::string fromSceneId = worldState.currentSceneId;
        audioManager.onRoomExit(sceneDatabase.getSceneAudio(fromSceneId), state.sceneId);

        if (!worldState.restore(state, conversationMgr, milestoneMgr, inventoryMgr))
            return false;

        sceneController.getActiveScene().loadFromStruct(worldState.currentSceneId, locationStruct);
        syncFromActiveScene();

        narrativeNotebook.getNarrativeText() = worldState.narrativeText;
        narrativeNotebook.resetNarrativeScroll();
        narrativeNotebook.invalidateLayout();
        narrativeNotebook.getChoiceHitAreas().clear();

        closeAllUiPanels();

        conversationMgr.onEnterScene(worldState.currentSceneId, sceneDatabase.getSpeakConfig(worldState.currentSceneId));

        ConversationPersistState conversationSnapshot;
        conversationMgr.exportPersistState(conversationSnapshot);
        progressionService.syncFromLegacyState(
            worldState.storyFlags,
            conversationSnapshot,
            [this](const std::string& itemId) { return inventoryMgr.hasItem(itemId); });
        evaluateMilestones();

        audioManager.onRoomEnter(sceneDatabase.getSceneAudio(worldState.currentSceneId), fromSceneId);
        trimNarrativeBuffer();
        refreshSceneImage();
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

    bool GameSession::quickSaveToDisk()
    {
        return saveGameService.quickSave(captureSaveState());
    }

    bool GameSession::namedSaveToDisk(const std::string& saveName)
    {
        const std::string trimmedName = trimSaveName(saveName);
        if (trimmedName.empty())
            return false;

        return saveGameService.namedSave(
            captureSaveState(),
            trimmedName,
            gameConfig.saves.maxNamedSaves);
    }

    bool GameSession::loadGameFromPath(const std::string& path)
    {
        SavedGameState state;
        if (!saveGameService.loadFromPath(path, state))
            return false;

        return applySaveState(state);
    }

    void GameSession::showTransientMessage(const std::string& message, float durationSeconds)
    {
        transientMessage = message;
        transientMessageTimer = durationSeconds;
    }

    void GameSession::updateTransientMessage(float deltaSeconds)
    {
        if (transientMessageTimer <= 0.0f)
            return;

        transientMessageTimer -= deltaSeconds;
        if (transientMessageTimer <= 0.0f)
            transientMessage.clear();
    }

    void GameSession::drawTransientMessage() const
    {
        if (transientMessage.empty())
            return;

        const float messageFontSize = 32.0f;
        const int messageSpacing = 3;
        const Vector2 textSize = MeasureTextEx(
            descriptionFont,
            transientMessage.c_str(),
            messageFontSize,
            messageSpacing);
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

        const Color panelBorder = uiBackdrop.isActive()
            ? uiBackdrop.panelBorderColor()
            : Color{168, 138, 72, 255};
        if (uiBackdrop.isActive())
            uiBackdrop.drawPanel(panel, 0.2f, 8);
        else
            DrawRectangleRounded(panel, 0.2f, 8, {28, 26, 34, 230});
        DrawRoundedBorder(panel, 0.2f, 8, 2.0f, panelBorder);

        DrawTextEx(
            descriptionFont,
            transientMessage.c_str(),
            { panel.x + padX, panel.y + padY },
            messageFontSize,
            messageSpacing,
            {228, 220, 198, 255});
    }

    void GameSession::handleQuickSaveInput()
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

    void GameSession::applyGrantedStoryFlag(const std::string& flag)
    {
        if (flag.empty())
            return;

        worldState.storyFlags.insert(flag);
        if (flag == "saloon_interior:chose_water"
            || flag == "saloon_interior:chose_usual"
            || flag == "saloon_interior:room_tab")
        {
            worldState.markActorKnown("bartender");
        }
        if (flag == "saloon_interior:room_tab"
            || flag == "saloon_interior:blue_woman_hired")
        {
            worldState.markActorKnown("burgundy_woman");
        }
        if (flag == "saloon_interior:room_tab")
        {
            worldState.saloonRoomPurchasedDay = worldState.day;
            if (worldState.storyFlags.count("saloon_interior:chose_usual") == 0
                && worldState.storyFlags.count("saloon_interior:chose_water") == 0)
            {
                worldState.storyFlags.insert("saloon_interior:chose_water");
            }
        }

        if (conversationMgr.isPhaseComplete("blue_woman"))
            worldState.storyFlags.insert("saloon_balcony:blue_woman_done");
    }

    void GameSession::handleDevOverlayInput()
    {
        if (!IsKeyPressed(KEY_S))
            return;

        const bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (!ctrlHeld || !shiftHeld)
            return;

        devOverlayVisible = !devOverlayVisible;
    }

    void GameSession::drawDevOverlay() const
    {
        if (!devOverlayVisible)
            return;

        const std::string dayText = "Day: " + std::to_string(worldState.day);
        const std::string countText = "Count: " + std::to_string(worldState.actionCount);
        const std::vector<SceneActorDef> sceneActors =
            sceneDatabase.getSceneActors(worldState.currentSceneId);

        std::vector<std::string> overlayLines;
        overlayLines.push_back(dayText);
        overlayLines.push_back(countText);
        for (const SceneActorDef& actor : sceneActors)
        {
            overlayLines.push_back(
                actor.name + ": " + std::to_string(worldState.actorOpinionOf(actor.id)));
        }

        const float fontSize = 22.0f;
        const int spacing = 2;
        const float padX = 14.0f;
        const float padY = 10.0f;
        const float lineGap = 6.0f;
        float contentWidth = 0.0f;
        float contentHeight = 0.0f;
        std::vector<Vector2> lineSizes;
        lineSizes.reserve(overlayLines.size());

        for (const std::string& line : overlayLines)
        {
            const Vector2 lineSize = MeasureTextEx(descriptionFont, line.c_str(), fontSize, spacing);
            lineSizes.push_back(lineSize);
            contentWidth = std::max(contentWidth, lineSize.x);
            contentHeight += lineSize.y;
        }

        if (overlayLines.size() > 1)
            contentHeight += lineGap * (float)(overlayLines.size() - 1);

        const float panelWidth = contentWidth + padX * 2.0f;
        const float panelHeight = contentHeight + padY * 2.0f;
        const float margin = 12.0f;
        const Rectangle panel = {
            margin,
            (float)screenHeight - panelHeight - margin,
            panelWidth,
            panelHeight
        };

        const Color panelBorder = uiBackdrop.isActive()
            ? uiBackdrop.panelBorderColor()
            : Color{168, 138, 72, 255};
        if (uiBackdrop.isActive())
            uiBackdrop.drawPanel(panel, 0.2f, 8);
        else
            DrawRectangleRounded(panel, 0.2f, 8, {28, 26, 34, 51});
        DrawRoundedBorder(panel, 0.2f, 8, 2.0f, panelBorder);

        const float textX = panel.x + padX;
        float textY = panel.y + padY;
        for (size_t lineIndex = 0; lineIndex < overlayLines.size(); ++lineIndex)
        {
            DrawTextEx(
                descriptionFont,
                overlayLines[lineIndex].c_str(),
                { textX, textY },
                fontSize,
                spacing,
                WHITE);
            textY += lineSizes[lineIndex].y + lineGap;
        }
    }

    void GameSession::handleSaveLoadMenuInput()
    {
        if (IsKeyPressed(KEY_ESCAPE))
            uiCoordinator.closeSaveLoadMenu(saveLoadMenu, pauseMenu);

        if (saveLoadMenu.consumeBackRequest())
            uiCoordinator.closeSaveLoadMenu(saveLoadMenu, pauseMenu);

        std::string saveName;
        if (saveLoadMenu.consumeNamedSaveRequest(saveName))
        {
            if (namedSaveToDisk(saveName))
                pauseMenu.setStatusMessage("Game saved.");
            else
                pauseMenu.setStatusMessage("Save failed.");

            uiCoordinator.closeSaveLoadMenu(saveLoadMenu, pauseMenu);
        }

        std::string loadPath;
        if (saveLoadMenu.consumeLoadSlotRequest(loadPath))
        {
            if (loadGameFromPath(loadPath))
            {
                pauseMenu.setStatusMessage("Game loaded.");
                closeAllUiPanels();
                audioManager.setGameplayPaused(false);
            }
            else
                pauseMenu.setStatusMessage("Load failed.");
        }
    }

    void GameSession::handlePauseMenuInput()
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            if (saveLoadMenu.isOpen())
            {
                uiCoordinator.closeSaveLoadMenu(saveLoadMenu, pauseMenu);
            }
            else if (pauseMenu.isOpen())
            {
                if (pauseMenu.isConfigPanel())
                    pauseMenu.showMainPanel();
                else
                {
                    uiCoordinator.closePauseMenu(pauseMenu, saveLoadMenu);
                    audioManager.setGameplayPaused(false);
                }
            }
            else
            {
                openUiMode(UiMode::Pause);
                audioManager.setGameplayPaused(true);
            }
        }

        if (!saveLoadMenu.isOpen())
            pauseMenu.update(GetFrameTime());

        if (pauseMenu.consumeOpenSaveMenuRequest())
            uiCoordinator.openSaveMenu(saveLoadMenu);

        if (pauseMenu.consumeOpenLoadMenuRequest())
            uiCoordinator.openLoadMenu(saveLoadMenu);

        if (pauseMenu.consumeQuitRequest())
            quitRequested = true;
    }

    void GameSession::update()
    {
        if (!initialFrameComplete)
        {
            initialFrameComplete = true;
        }
        else if (deferInitialRoomAudio)
        {
            audioManager.onRoomEnter(sceneDatabase.getSceneAudio(worldState.currentSceneId));
            deferInitialRoomAudio = false;
        }

        audioManager.update(GetFrameTime());
        overlayMgr.update(GetFrameTime());
        updateTransientMessage(GetFrameTime());
        handleQuickSaveInput();
        handleDevOverlayInput();
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

        syncNarrativeContext();
        updateInventoryLayout();
        updateActionAvailability();

        if (overlayMgr.isSequenceActive())
        {
            handleNarrativeScrollInput();
            return;
        }

        if (!inventoryMgr.isExaminingItem() && narrativeNotebook.getPage() == NotebookPage::CaseNotes)
            handleNarrativeChoiceInput();

        handleNotebookNavInput();

        buttonMgr.update();

        if (buttonMgr.consumeMoveOrActionButtonClick())
            audioManager.stopDialog();

        if (buttonMgr.consumeInventoryButtonClick())
        {
            if (inventoryMgr.isOpen())
                closeAllUiPanels();
            else
                openUiMode(UiMode::Inventory);

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
                closeAllUiPanels();
            else
            {
                refreshTakeItems();
                openUiMode(UiMode::Take);
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

        if (speakTargetMgr.isOpen())
        {
            speakTargetMgr.update();
            processPendingSpeakTargets();
            handleNarrativeScrollInput();
            updateActionAvailability();
            return;
        }

        if (inventoryMgr.isOpen())
        {
            inventoryMgr.update();
            if (inventoryMgr.consumeItemCombinationApplied())
            {
                const std::string narrative = inventoryMgr.consumePendingCombinationNarrative();
                if (!narrative.empty())
                    appendNarrativeSection("Using:", narrative);
                worldState.recordAction();
            }
            handleInventoryDropInput();

            if (buttonMgr.consumeBackwardButtonClick() && inventoryMgr.isExaminingItem())
            {
                clearItemExamineAudio();
                const InventoryItem* examinedItem = inventoryMgr.getSelectedItem();
                if (examinedItem != nullptr)
                {
                    const ItemDef* examinedDef = itemDatabase.getDef(examinedItem->id);
                    if (examinedDef != nullptr && !examinedDef->examineRevealFlag.empty())
                    {
                        inventoryMgr.applyExamineRevealFlag(
                            examinedItem->id,
                            examinedDef->examineRevealFlag);
                    }
                }
                inventoryMgr.returnToItemList();
                narrativeNotebook.resetInventoryExamineScroll();
                updateActionAvailability();
            }
            else if (buttonMgr.consumeUseButtonClick())
                useSelectedInventoryItem();
            else if (buttonMgr.consumeExamineButtonClick() && inventoryMgr.canExamineSelectedItem())
            {
                inventoryMgr.examineSelectedItem();
                const InventoryItem* examinedItem = inventoryMgr.getSelectedItem();
                if (examinedItem != nullptr)
                    playItemExamineAudio(*examinedItem);
                narrativeNotebook.resetInventoryExamineScroll();
                updateActionAvailability();
                worldState.recordAction();
            }
            if (inventoryMgr.isExaminingItem())
                handleInventoryExamineScrollInput();
            else if (narrativeNotebook.getPage() == NotebookPage::Todo)
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
            appendExamineDetails();

        if (!conversationMgr.isAwaitingChoice() && buttonMgr.consumeSpeakButtonClick())
            handleSpeak();

        if (!conversationMgr.isAwaitingChoice() && buttonMgr.consumeUseButtonClick())
        {
            if (!getAvailableInteractions().empty())
            {
                if (interactionMgr.isOpen())
                    closeAllUiPanels();
                else
                {
                    refreshInteractions();
                    openUiMode(UiMode::Interact);
                    updateInventoryLayout();
                }
                updateActionAvailability();
            }
            else
            {
                appendUseDetails();
            }
        }

        if (narrativeNotebook.getPage() == NotebookPage::Todo)
            handleTodoScrollInput();
        else
            handleNarrativeScrollInput();
    }



    void GameSession::draw() const
    {
        const_cast<GameSession*>(this)->syncNarrativeContext();
        ClearBackground(BLACK);

        drawMainImage();
        overlayMgr.draw(getMainImageBounds());

        const Rectangle dialog = getDialogBounds();
        drawNotebookBackdrop(dialog);

        if (inventoryMgr.isOpen() && inventoryMgr.isExaminingItem())
        {
            drawInventoryExamineView();
            drawInventoryExamineScrollbar();
        }
        else if (narrativeNotebook.getPage() == NotebookPage::Todo)
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
        else if (speakTargetMgr.isOpen())
            speakTargetMgr.draw();

        buttonMgr.draw();
        pauseMenu.draw();
        saveLoadMenu.draw();
        drawTransientMessage();
        dropConfirmMgr.draw();
        drawDevOverlay();
    }
    void GameSession::openUiMode(UiMode mode)
    {
        uiCoordinator.open(
            mode,
            inventoryMgr,
            takeMgr,
            interactionMgr,
            speakTargetMgr,
            dropConfirmMgr,
            pauseMenu,
            saveLoadMenu);
    }

    void GameSession::closeAllUiPanels()
    {
        uiCoordinator.closeAll(
            inventoryMgr,
            takeMgr,
            interactionMgr,
            speakTargetMgr,
            dropConfirmMgr,
            pauseMenu,
            saveLoadMenu);
    }

    void GameSession::syncNarrativeContext()
    {
        narrativeNotebook.setLayout(textBox, fullDialogHeight, isSidePanelOpen());
        narrativeNotebook.setContext(
            &conversationMgr,
            &progressionService,
            &worldState.committedPlayerDialogLines,
            worldState.playerStats.walletCash,
            canUseNotebookNav(),
            [this](const std::string& choiceId) { resolveDialogChoice(choiceId); });
    }

    void GameSession::trimNarrativeBuffer() { narrativeNotebook.trimBuffer(); }
    void GameSession::appendNarrativeSection(const char* header, const std::string& details)
    {
        narrativeNotebook.appendSection(header, details);
    }
    void GameSession::appendNarrativeSketch(const std::string& sketchPath)
    {
        narrativeNotebook.appendSketch(sketchPath);
    }
    void GameSession::appendChoiceLinesToNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& scrollAnchorLine)
    {
        narrativeNotebook.appendChoiceLines(choices, scrollAnchorLine);
    }
    void GameSession::stripDialogChoiceLinesFromNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText)
    {
        narrativeNotebook.stripChoiceLines(choices, keepLineText);
    }
    void GameSession::scrollNarrativeToHeader(const char* header)
    {
        narrativeNotebook.scrollToHeader(header);
    }
    void GameSession::scrollNarrativeToLine(const std::string& lineText, bool lastOccurrence)
    {
        narrativeNotebook.scrollToLine(lineText, lastOccurrence);
    }
    void GameSession::rebuildNarrativeLayout() const { narrativeNotebook.invalidateLayout(); }
    void GameSession::handleNarrativeChoiceInput() { narrativeNotebook.handleNarrativeChoiceInput(); }
    void GameSession::handleNarrativeScrollInput() { narrativeNotebook.handleNarrativeScrollInput(); }
    void GameSession::handleInventoryExamineScrollInput()
    {
        narrativeNotebook.handleInventoryExamineScrollInput();
    }
    void GameSession::handleNotebookNavInput() { narrativeNotebook.handleNotebookNavInput(); }
    void GameSession::handleTodoScrollInput() { narrativeNotebook.handleTodoScrollInput(); }
    bool GameSession::canUseNotebookNav() const
    {
        return !dropConfirmMgr.isOpen() && !saveLoadMenu.isOpen() && !pauseMenu.isOpen();
    }
    void GameSession::drawNarrativeText() const { narrativeNotebook.drawNarrativeText(); }
    void GameSession::drawNarrativeScrollbar() const { narrativeNotebook.drawNarrativeScrollbar(); }
    void GameSession::drawInventoryExamineView() const
    {
        narrativeNotebook.drawInventoryExamineView(inventoryMgr.getSelectedItem());
    }
    void GameSession::drawInventoryExamineScrollbar() const
    {
        narrativeNotebook.drawInventoryExamineScrollbar();
    }
    void GameSession::drawNotebookBackdrop(const Rectangle& bounds) const
    {
        narrativeNotebook.drawNotebookBackdrop(bounds);
    }
    void GameSession::drawNotebookHeader(const Rectangle& bounds) const
    {
        narrativeNotebook.drawNotebookHeader(bounds);
    }
    void GameSession::drawNotebookNavButtons(const Rectangle& bounds) const
    {
        narrativeNotebook.drawNotebookNavButtons(bounds);
    }
    void GameSession::drawQuestTodoPage() const { narrativeNotebook.drawTodoPage(); }
    void GameSession::drawTodoScrollbar() const { narrativeNotebook.drawTodoScrollbar(); }
    Rectangle GameSession::getDialogBounds() const
    {
        return narrativeNotebook.getDialogBounds();
    }
    std::vector<ConversationChoiceDef> GameSession::filterAvailableChoices(
        const std::vector<ConversationChoiceDef>& choices) const
    {
        return narrativeNotebook.filterChoices(choices);
    }


}
