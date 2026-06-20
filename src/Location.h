#ifndef LOCATION_H
#define LOCATION_H

#include <AudioManager.h>
#include <ConversationManager.h>
#include <ConversationStruct.h>
#include <GameConfig.h>
#include <InventoryMgr.h>
#include <PauseMenuMgr.h>
#include <SaveGame.h>
#include <TakeMgr.h>
#include <TakeableItemDef.h>
#include <LocationStruct.h>
#include <ButtonMgr.h>
#include <SceneLoader.h>
#include <raylib.h>
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

class Location
{
    public:
    Location(
        const LocationStruct& locationStruct,
        Vector2 screenSize,
        SceneDatabase& sceneDatabase,
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
    void relayoutForScreenSize(int width, int height);
    SavedGameState captureSaveState() const;
    bool applySaveState(const SavedGameState& state);
    bool saveGameToDisk();
    bool loadGameFromDisk();
    void resolveDialogChoice(const std::string& choiceId);
    void resolveCombatEncounter(const std::string& encounterId);
    void processSpeakResult(const SpeakResult& result);
    void grantConversationItem(const GrantedInventoryItemDef& granted);
    void playDialogAudio(const SpeakResult& result);
    void applyStatusEffects(const std::vector<StatusEffect>& effects);
    void handleNarrativeChoiceInput();
    void appendChoiceLinesToNarrative(const std::vector<ConversationChoiceDef>& choices);
    void scrollToPendingDialogChoices();
    void stripDialogChoiceLinesFromNarrative(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText = "");
    void applyLucidityCollapseRestart();
    bool isDialogChoiceLine(const std::string& line) const;
    Color narrativeLineColor(const std::string& line) const;
    void applyLocationStruct(const LocationStruct& locationStruct, const std::string& fromRoom = "");
    void tryMove(const std::string& direction);
    void trimNarrativeBuffer();
    void rebuildNarrativeLayout() const;
    void handleNarrativeScrollInput();
    void handleInventoryExamineScrollInput();
    void updateInventoryLayout();
    void refreshTakeItems();
    void addTakenItemToInventory(const TakeableItemDef& taken);
    void processPendingTakes();
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
    AudioManager& audioManager;
    GameConfig& gameConfig;
    PauseMenuMgr pauseMenu;
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
    const int yOffset = 36;

    Rectangle textBox;
    Rectangle buttonBox;
    float fullDialogHeight;
    ButtonMgr buttonMgr;
    InventoryMgr inventoryMgr;
    TakeMgr takeMgr;
    std::set<std::string> takenItemKeys;

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

    bool deferInitialRoomAudio = true;
    bool initialFrameComplete = false;
};

}

#endif /* LOCATION_H */
