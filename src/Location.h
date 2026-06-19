#ifndef LOCATION_H
#define LOCATION_H

#include <ConversationManager.h>
#include <ConversationStruct.h>
#include <InventoryMgr.h>
#include <LocationStruct.h>
#include <ButtonMgr.h>
#include <RoomLoader.h>
#include <raylib.h>
#include <set>
#include <string>
#include <vector>
namespace testgame
{

struct DialogChoice
{
    std::string id;
    std::string lineText;
};

struct NarrativeChoiceHitArea
{
    std::string id;
    Rectangle bounds;
};

class Location
{
    public:
    Location(const LocationStruct& locationStruct, Vector2 screenSize, RoomDatabase& roomDatabase, const std::string& roomId);

    virtual ~Location();

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
    void resolveDialogChoice(const std::string& choiceId);
    void processSpeakResult(const SpeakResult& result);
    void applyStatusEffects(const std::vector<StatusEffect>& effects);
    void handleNarrativeChoiceInput();
    void appendDialogChoices(const std::vector<DialogChoice>& choices);
    void applyLucidityCollapseRestart();
    bool isDialogChoiceLine(const std::string& line) const;
    Color narrativeLineColor(const std::string& line) const;
    void applyLocationStruct(const LocationStruct& locationStruct);
    void tryMove(const std::string& direction);
    void trimNarrativeBuffer();
    void rebuildNarrativeLayout() const;
    void handleNarrativeScrollInput();
    void handleInventoryExamineScrollInput();
    void updateInventoryLayout();
    void drawNarrativeText() const;
    void drawNarrativeScrollbar() const;
    void drawInventoryExamineView() const;
    void drawInventoryExamineScrollbar() const;
    void drawMainImage() const;
    Rectangle getMainImageBounds() const;
    Rectangle getDialogBounds() const;
    Rectangle getInventoryPanelBounds() const;
    void appendNarrativeSection(const char* header, const std::string& details);
    void updateActionAvailability();
    bool tryApplyStatusDeltas(
        const std::string& actionKey,
        float healthDelta,
        float energyDelta,
        float tenacityDelta,
        float lucidityDelta,
        bool allowRepeat);
    void scrollNarrativeToHeader(const char* header);
    float getNarrativeLineOffsetY(const std::string& lineText, bool lastOccurrence) const;
    float getNarrativeVisibleHeight() const;
    float getNarrativeLineHeight() const;
    float getNarrativeWrapWidth() const;
    void layoutWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY, bool draw, float scrollY, Color lineColor) const;
    bool isBoldNarrativeHeader(const std::string& line) const;

    static const int kMaxNarrativeLines = 500;
    static const float kScrollbarWidth;

    const int screenWidth;
    const int screenHeight;

    RoomDatabase& roomDatabase;
    std::string currentRoomId;
    
    Texture2D locationImage;
    bool ownsLocationImage = true;
    bool isUnderConstruction = false;
    std::string previousRoomId;
    std::string baseDescription;
    std::string narrativeText;
    std::string examineDetails;
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
    bool hasExaminedCurrentRoom = false;
    bool hasSpokenInCurrentRoom = false;
    bool hasUsedInCurrentRoom = false;
    bool awaitingDialogChoice = false;
    std::vector<DialogChoice> pendingDialogChoices;
    ConversationManager conversationMgr;
    mutable std::vector<NarrativeChoiceHitArea> narrativeChoiceHitAreas;

    float health = 90.0f;
    float energy = 20.0f;
    float tenacity = 50.0f;
    float lucidity = 30.0f;
    std::set<std::string> consumedStatusActions;

    float fontSize = 20;
    const bool wordWrap = true;
    const int spacing = 2;
    const Color textColor = DARKGRAY;
    const int xOffset = 14;
    const int yOffset = 14;

    const Rectangle textBox;
    const Rectangle buttonBox;
    const float fullDialogHeight;
    ButtonMgr buttonMgr;
    InventoryMgr inventoryMgr;

    float narrativeScrollY = 0.0f;
    float inventoryExamineScrollY = 0.0f;
    mutable float inventoryExamineContentHeight = 0.0f;
    bool inventoryExamineScrollbarDragging = false;
    float inventoryExamineScrollbarDragOffsetY = 0.0f;
    mutable bool narrativeLayoutDirty = true;
    bool scrollbarDragging = false;
    float scrollbarDragOffsetY = 0.0f;

    mutable float narrativeContentHeight = 0.0f;
};

}

#endif /* LOCATION_H */
