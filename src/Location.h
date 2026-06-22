#ifndef LOCATION_H
#define LOCATION_H

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
#include <SceneInteractionDef.h>

#include <LocationStruct.h>
#include <ButtonMgr.h>
#include <SceneLoader.h>
#include <raylib.h>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace testgame
{

struct NarrativeChoiceHitArea
{
    std::string id;
    Rectangle bounds;
};

struct NarrativeSketchPlacement
{
    std::string path;
    float yOffset = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

enum class NotebookPage
{
    CaseNotes,
    Todo
};

class Location
{
    public:
    Location(
        const LocationStruct& locationStruct,
        Vector2 screenSize,
        SceneDatabase& sceneDatabase,
        const ItemDatabase& itemDatabase,
        const ItemCombinationDatabase& itemCombinationDatabase,
        const MilestoneDatabase& milestoneDatabase,
        AudioManager& audioManager,
        GameConfig& gameConfig,
        const std::string& sceneId,
        const std::string& configPath = "resources/game_config.json");

    virtual ~Location();

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
    void relayoutForScreenSize(int width, int height);
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
    void applyStatusEffects(const std::vector<StatusEffect>& effects);
    void handleNarrativeChoiceInput();
    void appendChoiceLinesToNarrative(const std::vector<ConversationChoiceDef>& choices);
    std::vector<ConversationChoiceDef> filterAvailableChoices(
        const std::vector<ConversationChoiceDef>& choices) const;
    void appendNarrativeSketch(const std::string& sketchPath);
    bool isNarrativeSketchLine(const std::string& line) const;
    std::string narrativeSketchPathFromLine(const std::string& line) const;
    float getNarrativeSketchHeight(const std::string& sketchPath) const;
    float getNarrativeSketchDisplaySize(const Texture2D& texture, float& outWidth, float& outHeight) const;
    Texture2D getOrLoadNarrativeSketchTexture(const std::string& sketchPath) const;
    void layoutNarrativeSketch(const std::string& sketchPath, float& textOffsetY) const;
    void drawNarrativeSketches() const;
    std::string normalizeNarrativeLine(std::string line) const;
    void scrollToPendingDialogChoices();
    void stripDialogChoiceLinesFromNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText = "");
    void applyLucidityCollapseRestart();
    void evaluateMilestones();
    bool isDialogChoiceLine(const std::string& line) const;
    Color narrativeLineColor(const std::string& line) const;
    void applyLocationStruct(const LocationStruct& locationStruct, const std::string& fromRoom = "");
    void tryMove(const std::string& direction);
    bool hasLightSourceInInventory() const;
    bool canUseExit(const std::string& direction, std::string& outBlockedDetails) const;
    void appendBlockedMovementMessage(const std::string& details);
    void trimNarrativeBuffer();
    void rebuildNarrativeLayout() const;
    void handleNarrativeScrollInput();
    void handleInventoryExamineScrollInput();
    void updateInventoryLayout();
    void refreshTakeItems();
    void refreshInteractions();
    void addTakenItemToInventory(const TakeableItemDef& taken);
    void processPendingTakes();
    void processPendingInteractions();
    void applyInteraction(const SceneInteractionDef& interaction);
    void applyDirectUse();
    void transitionToScene(const std::string& sceneId);
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
    void ensureNotebookPaperTexture() const;
    Rectangle getMainImageBounds() const;
    Rectangle getDialogBounds() const;
    Rectangle getInventoryPanelBounds() const;
    void appendNarrativeSection(const char* header, const std::string& details);
    void updateActionAvailability();
    bool tryApplyStatusEffect(const StatusEffect& effect, bool allowRepeat);
    void scrollNarrativeToHeader(const char* header);
    void scrollNarrativeToLine(const std::string& lineText, bool lastOccurrence);
    void rebuildNarrativeChoiceHitAreas() const;
    float getNarrativeLineOffsetY(const std::string& lineText, bool lastOccurrence) const;
    Rectangle getNotebookContentBounds() const;
    float getNarrativeVisibleHeight() const;
    float getNarrativeLineHeight() const;
    float getNarrativeWrapWidth() const;
    void layoutWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY, bool draw, float scrollY, Color lineColor) const;
    bool isBoldNarrativeHeader(const std::string& line) const;
    bool isBoldNarrativeLine(const std::string& line) const;
    bool hasExaminedScene(const std::string& sceneId) const;
    bool canUseInCurrentScene() const;

    static const int kMaxNarrativeLines = 500;
    static const float kScrollbarWidth;

    int screenWidth;
    int screenHeight;

    SceneDatabase& sceneDatabase;
    const ItemDatabase& itemDatabase;
    const ItemCombinationDatabase& itemCombinationDatabase;
    MilestoneManager milestoneMgr;
    AudioManager& audioManager;
    GameConfig& gameConfig;
    PauseMenuMgr pauseMenu;
    SaveLoadMenuMgr saveLoadMenu;
    DropConfirmMgr dropConfirmMgr;
    std::string gameConfigPath;
    bool quitRequested = false;
    std::string currentSceneId;
    
    Texture2D locationImage;
    bool ownsLocationImage = true;
    bool isUnderConstruction = false;
    std::string previousSceneId;
    std::string baseDescription;
    std::string narrativeText;
    std::string examineDetails;
    std::string examineFlag;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    bool useRequiresExamine = true;
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
    bool hasSpokenInCurrentScene = false;
    bool hasUsedInCurrentScene = false;
    std::set<std::string> examinedSceneIds;
    std::set<std::string> usedSceneIds;
    std::set<std::string> committedPlayerDialogLines;
    ConversationManager conversationMgr;
    mutable std::vector<NarrativeChoiceHitArea> narrativeChoiceHitAreas;

    float health = 90.0f;
    float energy = 20.0f;
    float resolve = 50.0f;
    float lucidity = 30.0f;
    float charisma = 50.0f;
    float walletCash = 20.0f;
    std::set<std::string> consumedStatusActions;
    std::set<std::string> storyFlags;

    float fontSize = 32.0f;
    const bool wordWrap = true;
    const int spacing = 3;
    const Color textColor = {32, 42, 68, 255};
    const int xOffset = 78;
    const float notebookHeaderReserve = 52.0f;
    const float notebookContentBottomPad = 18.0f;
    const float notebookHeaderFontSize = 22.0f;

    NotebookPage notebookPage = NotebookPage::CaseNotes;
    float todoScrollY = 0.0f;
    bool todoScrollbarDragging = false;
    float todoScrollbarDragOffsetY = 0.0f;
    mutable float todoContentHeight = 0.0f;

    Rectangle textBox;
    Rectangle buttonBox;
    float fullDialogHeight;
    ButtonMgr buttonMgr;
    InventoryMgr inventoryMgr;
    TakeMgr takeMgr;
    InteractionMgr interactionMgr;
    std::set<std::string> takenItemKeys;
    std::set<std::string> usedInteractionKeys;
    std::map<std::string, std::vector<TakeableItemDef>> droppedItemsByScene;

    float narrativeScrollY = 0.0f;
    float inventoryExamineScrollY = 0.0f;
    mutable float inventoryExamineContentHeight = 0.0f;
    bool inventoryExamineScrollbarDragging = false;
    float inventoryExamineScrollbarDragOffsetY = 0.0f;
    mutable bool narrativeLayoutDirty = true;
    bool scrollbarDragging = false;
    float scrollbarDragOffsetY = 0.0f;

    mutable float narrativeContentHeight = 0.0f;

    mutable Texture2D notebookPaperTexture{};
    mutable bool notebookPaperTextureReady = false;
    mutable std::map<std::string, Texture2D> narrativeSketchTextures;
    mutable std::vector<NarrativeSketchPlacement> narrativeSketchPlacements;

    bool deferInitialRoomAudio = true;
    bool initialFrameComplete = false;
    std::string transientMessage;
    float transientMessageTimer = 0.0f;
};

}

#endif /* LOCATION_H */
