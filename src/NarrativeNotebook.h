#ifndef NARRATIVE_NOTEBOOK_H
#define NARRATIVE_NOTEBOOK_H

#include <ConversationStruct.h>
#include <InventoryItem.h>
#include <ScrollPanel.h>
#include <functional>
#include <map>
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

class ConversationManager;
class ProgressionService;

class NarrativeNotebook
{
    public:
    static const float kScrollbarWidth;
    static const int kMaxNarrativeLines;

    NarrativeNotebook();
    ~NarrativeNotebook();

    void setFonts(Font descriptionFont, Font boldFont);
    void setLayout(Rectangle textBox, float fullDialogHeight, bool sidePanelOpen);
    void setAssetRoot(const std::string& root);
    void setContext(
        const ConversationManager* conversation,
        const ProgressionService* progression,
        const std::set<std::string>* committedLines,
        float walletCash,
        bool navEnabled,
        const std::function<void(const std::string&)>& onChoiceSelected);
    void unloadSketchTextures();

    NotebookPage getPage() const { return notebookPage; }
    void setPage(NotebookPage page) { notebookPage = page; }
    bool isTodoPage() const { return notebookPage == NotebookPage::Todo; }

    std::string& getNarrativeText() { return narrativeText; }
    const std::string& getNarrativeText() const { return narrativeText; }

    void trimBuffer();
    void appendSection(const char* header, const std::string& details);
    void appendSketch(const std::string& sketchPath);
    void appendChoiceLines(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& scrollAnchorLine = "");
    void stripChoiceLines(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText = "");
    void scrollToHeader(const char* header);
    void scrollToLine(const std::string& lineText, bool lastOccurrence);
    void scrollToPendingChoices(const std::string& preferredLine = "");
    void resetNarrativeScroll();
    void resetInventoryExamineScroll();
    void invalidateLayout() const { narrativeLayoutDirty = true; }

    void handleNarrativeScrollInput();
    void handleTodoScrollInput();
    void handleInventoryExamineScrollInput();
    void handleNotebookNavInput();
    void handleNarrativeChoiceInput();
    bool canUseNotebookNav() const;

    void drawNotebookBackdrop(const Rectangle& bounds) const;
    void drawNotebookHeader(const Rectangle& bounds) const;
    void drawNotebookNavButtons(const Rectangle& bounds) const;
    void drawNarrativeText() const;
    void drawNarrativeScrollbar() const;
    void drawTodoPage() const;
    void drawTodoScrollbar() const;
    void drawInventoryExamineView(const InventoryItem* item) const;
    void drawInventoryExamineScrollbar() const;

    std::vector<NarrativeChoiceHitArea>& getChoiceHitAreas() const
    {
        return narrativeChoiceHitAreas;
    }

    Rectangle getDialogBounds() const;
    std::vector<ConversationChoiceDef> filterChoices(
        const std::vector<ConversationChoiceDef>& choices) const;

    private:
    float getNarrativeLineHeight() const;
    float getNarrativeVisibleHeight() const;
    float getNarrativeWrapWidth() const;
    Rectangle getNotebookContentBounds() const;
    void rebuildNarrativeLayout() const;
    void rebuildNarrativeChoiceHitAreas() const;
    void layoutWrappedParagraph(
        const char* text,
        Font font,
        float paragraphFontSize,
        float& textOffsetY,
        bool draw,
        float scrollY,
        Color lineColor) const;
    std::string normalizeNarrativeLine(std::string line) const;
    bool isBoldNarrativeHeader(const std::string& line) const;
    bool isBoldNarrativeLine(const std::string& line) const;
    bool isDialogChoiceLine(const std::string& line) const;
    bool isNarrativeSketchLine(const std::string& line) const;
    std::string narrativeSketchPathFromLine(const std::string& line) const;
    Color narrativeLineColor(const std::string& line) const;
    float getNarrativeLineOffsetY(const std::string& lineText, bool lastOccurrence) const;
    void ensureNotebookPaperTexture() const;
    float getNarrativeSketchHeight(const std::string& sketchPath) const;
    float getNarrativeSketchDisplaySize(
        const Texture2D& texture,
        float& outWidth,
        float& outHeight) const;
    Texture2D getOrLoadNarrativeSketchTexture(const std::string& sketchPath) const;
    void layoutNarrativeSketch(const std::string& sketchPath, float& textOffsetY) const;
    void drawNarrativeSketches() const;

    Rectangle textBox{};
    float fullDialogHeight = 0.0f;
    bool sidePanelOpen = false;
    std::string primaryAssetRoot = ".";

    const ConversationManager* conversationMgr = nullptr;
    const ProgressionService* progressionService = nullptr;
    const std::set<std::string>* committedPlayerDialogLines = nullptr;
    float walletCash = 0.0f;
    bool notebookNavEnabled = true;
    std::function<void(const std::string&)> onChoiceSelected;

    Font descriptionFont{};
    Font boldFont{};
    float fontSize = 32.0f;
    const bool wordWrap = true;
    const int spacing = 3;
    const Color textColor = {32, 42, 68, 255};
    const int xOffset = 78;
    const float notebookHeaderReserve = 52.0f;
    const float notebookContentBottomPad = 18.0f;
    const float notebookHeaderFontSize = 22.0f;

    NotebookPage notebookPage = NotebookPage::CaseNotes;
    std::string narrativeText;

    mutable ScrollPanel narrativeScroll;
    mutable ScrollPanel todoScroll;
    mutable ScrollPanel inventoryExamineScroll;

    mutable float narrativeContentHeight = 0.0f;
    mutable float todoContentHeight = 0.0f;
    mutable float inventoryExamineContentHeight = 0.0f;
    mutable bool narrativeLayoutDirty = true;
    mutable std::vector<NarrativeChoiceHitArea> narrativeChoiceHitAreas;

    mutable Texture2D notebookPaperTexture{};
    mutable bool notebookPaperTextureReady = false;
    mutable std::map<std::string, Texture2D> narrativeSketchTextures;
    mutable std::vector<NarrativeSketchPlacement> narrativeSketchPlacements;
};

}

#endif /* NARRATIVE_NOTEBOOK_H */