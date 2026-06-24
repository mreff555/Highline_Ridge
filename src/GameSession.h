#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include <AudioManager.h>
#include <ConversationManager.h>
#include <ConversationStruct.h>
#include <MilestoneManager.h>
#include <GameConfig.h>
#include <InventoryMgr.h>
#include <ItemCombinationDatabase.h>
#include <ItemDatabase.h>
#include <DropConfirmMgr.h>
#include <SaveLoadMenuMgr.h>
#include <PauseMenuMgr.h>
#include <SaveGame.h>
#include <TakeMgr.h>
#include <TakeableItemDef.h>
#include <InteractionMgr.h>
#include <SpeakTargetDef.h>
#include <SpeakTargetMgr.h>
#include <SceneInteractionDef.h>
#include <UiBackdrop.h>

#include <LocationStruct.h>
#include <ButtonMgr.h>
#include <SceneLoader.h>
#include <SceneOverlayMgr.h>
#include <NarrativeNotebook.h>
#include <ProgressionService.h>
#include <SaveGameService.h>
#include <SceneController.h>
#include <UiCoordinator.h>
#include <WorldState.h>
#include <raylib.h>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace testgame
{

class GameSession
{
    public:
    GameSession(
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
        const std::string& configPath = "resources/game_config.json");

    ~GameSession();

    bool shouldQuit() const { return quitRequested; }
    void applyDisplayConfig();
    void applyInputConfig();

    Texture2D getImage() const;
     char* getDescription() const;
    const Font getDescriptionFont() const;
    bool isUp() const;
    bool isDown() const;
    bool isForward() const;
    bool isBackward() const;
    bool isLeft() const;
    bool isRight() const;

    void update();
    void draw() const;
    void appendExamineDetails();
    void appendSpeakDetails();
    void appendUseDetails();

    private:
    void handleSpeak();
    void handleAttack();
    void handlePauseMenuInput();
    void handleSaveLoadMenuInput();
    void handleQuickSaveInput();
    void handleDevOverlayInput();
    void drawDevOverlay() const;
    void applyGrantedStoryFlag(const std::string& flag);
    void syncKnownActorsFromProgress();
    void relayoutForScreenSize(int width, int height);
    void syncNarrativeContext();
    void syncFromActiveScene();
    void refreshSceneImage();
    SavedGameState captureSaveState() const;
    bool applySaveState(const SavedGameState& state);
    bool quickSaveToDisk();
    bool namedSaveToDisk(const std::string& saveName);
    bool loadGameFromPath(const std::string& path);
    void showTransientMessage(const std::string& message, float durationSeconds = 2.5f);
    void updateTransientMessage(float deltaSeconds);
    void drawTransientMessage() const;
    void resolveDialogChoice(const std::string& choiceId);
    void resolveCombatEncounter(const std::string& encounterId);
    void processSpeakResult(const SpeakResult& result);
    void grantConversationItem(const GrantedInventoryItemDef& granted);
    InventoryItem buildInventoryItem(
        const std::string& defId,
        const ItemDefOverrides& overrides = {}) const;
    void playItemExamineAudio(const InventoryItem& item);
    void clearItemExamineAudio();
    bool canTakeFromExaminedItem() const;
    void takeFromExaminedItem();
    void playDialogAudio(const SpeakResult& result);
    void playInteractionTts(const SceneInteractionDef& interaction, bool includeAfter = false);
    void applyStatusEffects(const std::vector<StatusEffect>& effects);
    void handleNarrativeChoiceInput();
    void appendChoiceLinesToNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& scrollAnchorLine = "");
    std::vector<ConversationChoiceDef> filterAvailableChoices(
        const std::vector<ConversationChoiceDef>& choices) const;
    void appendNarrativeSketch(const std::string& sketchPath);
    void applyLucidityCollapseRestart();
    void evaluateMilestones();
    void applyLocationStruct(const LocationStruct& locationStruct, const std::string& fromRoom = "");
    void transitionToScene(const std::string& sceneId);
    void tryMove(const std::string& direction);
    void appendBlockedMovementMessage(const std::string& details);
    void trimNarrativeBuffer();
    void handleNarrativeScrollInput();
    void handleInventoryExamineScrollInput();
    void updateInventoryLayout();
    void refreshTakeItems();
    void refreshInteractions();
    void refreshSpeakTargets();
    void processPendingSpeakTargets();
    std::vector<SpeakTargetDef> getAvailableSpeakTargets() const;
    bool shouldOpenSpeakPicker() const;
    void addTakenItemToInventory(const TakeableItemDef& taken);
    void processPendingTakes();
    void processPendingInteractions();
    void applyInteraction(const SceneInteractionDef& interaction);
    void applyBedroomReadNewspaper(const SceneInteractionDef& interaction);
    void applyBedroomSleep(const SceneInteractionDef& interaction);
    void finishBedroomSleep(const SceneInteractionDef& interaction, bool blueWomanHired);
    void applySceneOverlays();
    void triggerOverlaySequence(
        const std::vector<OverlaySequenceStep>& sequence,
        std::function<void()> onComplete = nullptr);
    static std::string formatNewspaperDate(int day);
    void applyDirectUse();
    std::vector<SceneInteractionDef> getAvailableInteractions() const;
    std::string interactionKey(const std::string& interactionId) const;
    void handleInventoryDropInput();
    void dropInventoryItem(const std::string& itemId);
    TakeableItemDef takeableFromInventory(const InventoryItem& item) const;
    std::string buildExamineDetailsWithDroppedItems() const;
    std::vector<TakeableItemDef> getDroppedItemsInCurrentScene() const;
    bool isDroppedItemInCurrentScene(const std::string& itemId) const;
    void removeDroppedItem(const std::string& sceneId, const std::string& itemId);
    bool isSidePanelOpen() const;
    bool canTakeInCurrentScene() const;
    std::vector<TakeableItemDef> getAvailableTakeables() const;
    std::string takenItemKey(const std::string& itemId) const;
    void drawNarrativeText() const;
    void drawNarrativeScrollbar() const;
    void drawInventoryExamineView() const;
    void drawInventoryExamineScrollbar() const;
    void drawMainImage() const;
    void drawNotebookBackdrop(const Rectangle& bounds) const;
    void drawNotebookHeader(const Rectangle& bounds) const;
    void drawNotebookNavButtons(const Rectangle& bounds) const;
    void drawQuestTodoPage() const;
    void drawTodoScrollbar() const;
    void handleNotebookNavInput();
    void handleTodoScrollInput();
    bool canUseNotebookNav() const;
    Rectangle getMainImageBounds() const;
    Rectangle getDialogBounds() const;
    Rectangle getInventoryPanelBounds() const;
    void appendNarrativeSection(const char* header, const std::string& details);
    void updateActionAvailability();
    bool tryApplyStatusEffect(const StatusEffect& effect, bool allowRepeat);
    void scrollNarrativeToHeader(const char* header);
    void scrollNarrativeToLine(const std::string& lineText, bool lastOccurrence);
    void rebuildNarrativeLayout() const;
    void stripDialogChoiceLinesFromNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText = "");
    bool hasExaminedScene(const std::string& sceneId) const;
    bool canUseInCurrentScene() const;
    bool canUseSelectedInventoryItem() const;
    void useSelectedInventoryItem();
    void openUiMode(UiMode mode);
    void closeAllUiPanels();

    int screenWidth;
    int screenHeight;

    SceneDatabase& sceneDatabase;
    const ItemDatabase& itemDatabase;
    const ItemCombinationDatabase& itemCombinationDatabase;
    MilestoneManager milestoneMgr;
    AudioManager& audioManager;
    GameConfig& gameConfig;
    const UiBackdrop& uiBackdrop;
    PauseMenuMgr pauseMenu;
    SaveLoadMenuMgr saveLoadMenu;
    DropConfirmMgr dropConfirmMgr;
    std::string gameConfigPath;
    bool quitRequested = false;

    Texture2D locationImage{};
    bool ownsLocationImage = false;
    bool isUnderConstruction = false;
    std::string baseDescription;
    std::string examineDetails;
    std::string examineFlag;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    bool useRequiresExamine = true;
    bool useAdvancesDay = false;
    std::string useExit;
    Font descriptionFont;
    Font boldFont;
    ActionStruct baseActionFilter;
    
    bool up;
    bool down;
    bool forward;
    bool backward;
    bool left;
    bool right;
    ConversationManager conversationMgr;

    Rectangle textBox;
    Rectangle buttonBox;
    float fullDialogHeight;
    ButtonMgr buttonMgr;
    InventoryMgr inventoryMgr;
    TakeMgr takeMgr;
    InteractionMgr interactionMgr;
    SpeakTargetMgr speakTargetMgr;

    bool deferInitialRoomAudio = true;
    bool initialFrameComplete = false;
    std::string transientMessage;
    float transientMessageTimer = 0.0f;
    bool devOverlayVisible = false;

    mutable WorldState worldState;
    SceneController sceneController;
    SaveGameService saveGameService;
    ProgressionService progressionService;
    UiCoordinator uiCoordinator;
    NarrativeNotebook narrativeNotebook;
    SceneOverlayMgr overlayMgr;
};

}

#endif /* GAME_SESSION_H */