/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations flowchart canvas (Phase 1 shell): Start compass + stub nodes.
 ******************************************************************************/

#ifndef TIMBERLINE_DIALOG_FLOW_CANVAS_H
#define TIMBERLINE_DIALOG_FLOW_CANVAS_H

#include "DialogFlowTypes.h"
#include "DocumentWorkspace.h"
#include "FullscreenParchmentEditor.h"

#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct DialogFlowNode
{
    int id = 0;
    DialogNodeKind kind = DialogNodeKind::ActorDialog;
    float x = 0.0f; // world (canvas content) coords — top-left of card
    float y = 0.0f;
    std::string title; // override; empty → kind label

    // Type-specific authoring fields (editor-local until Phase 2 persist).
    std::string dialogText;
    std::string dialogTts;
    std::string playerDialogText;
    /** Actor/Player default TTS voice id; empty = Off (no default voice). */
    std::string defaultVoice;
    /** Relative MP3 path for this line's bag (resources/audio/tts/…). */
    std::string dialogTtsAudio;
    /** Multi-voice bake paths (ttsAudioSegments); empty when single-file. */
    std::vector<std::string> dialogTtsAudioSegments;
    /**
     * Phase revisit line (Actor Dialog opener only). Runtime uses resumeIntro when
     * returning to remaining top-level choices — not a first-visit sibling (#60).
     */
    std::string resumeIntroText;
    std::string resumeIntroTts;
    std::string resumeIntroVoice;
    std::string resumeIntroTtsAudio;
    /** ActorInventory node: bag key / openActorInventory actor id (#60). */
    std::string inventoryActorId;
    std::string eventId;
    std::string itemId;
    std::string combatantId;
    bool playerDeathPossible = false;
    std::vector<DialogStatModifier> winModifiers;

    /** JSON pointer into conversations.json (migration / round-trip identity). */
    std::string jsonPointer;
    std::string sourcePhaseId;
    std::string sourceChoiceId;
};

struct DialogFlowEdge
{
    int fromId = 0; // 0 = Start
    int fromChildIndex = 0;
    int toId = 0; // must be a real node (parent port)
};

enum DialogFlowMenuAction
{
    kFlowMenuNone = 0,
    kFlowMenuEditText,
    kFlowMenuEditTts,
    kFlowMenuEditPlayerText,
    kFlowMenuEditEventId,
    kFlowMenuEditItemId,
    kFlowMenuEditInventoryActor,
    kFlowMenuEditCombatant,
    kFlowMenuToggleDeathPossible,
    kFlowMenuOpenDefaultVoice,
    kFlowMenuDelete
};

struct DialogFlowCanvas
{
    DocumentWorkspace* docs = nullptr;
    std::string* selectionSceneId = nullptr;
    FullscreenParchmentEditor* parchment = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    float scrollX = 0.0f;
    float scrollY = 0.0f;
    int nextNodeId = 1;
    std::vector<DialogFlowNode> nodes;
    std::vector<DialogFlowEdge> edges;
    int selectedNodeId = -1; // -1 none; 0 = Start

    /** Palette drag: kind pending placement while mouse down. */
    bool placingFromPalette = false;
    DialogNodeKind placeKind = DialogNodeKind::ActorDialog;

    bool wiring = false;
    int wireFromId = 0;
    int wireFromChild = 0;

    bool draggingNode = false;
    int dragNodeId = -1;
    float dragGrabDX = 0.0f;
    float dragGrabDY = 0.0f;

    // Right-click context menu
    bool contextMenuOpen = false;
    int contextMenuNodeId = -1;
    Rectangle contextMenuBounds{0, 0, 0, 0};
    std::vector<std::string> contextMenuItems;
    std::vector<int> contextMenuActions; // DialogFlowMenuAction

    /** Flyout from "Default voice…" — Off + builtin Grok voices. */
    bool voiceMenuOpen = false;
    Rectangle voiceMenuBounds{0, 0, 0, 0};
    float voiceMenuScroll = 0.0f;
    int voiceMenuAnchorRow = -1; // context-menu row that opened the flyout
    static constexpr int kVoiceMenuVisibleRows = 10;

    Rectangle lastBounds{0, 0, 0, 0};
    Rectangle cleanUpBtn{0, 0, 0, 0};
    std::string status;

    std::array<Texture2D, static_cast<size_t>(DialogNodeKind::Count)> kindIcons{};
    bool iconsLoaded = false;
    std::string iconsResourceDir;

    /** Map nodes are icon + ports only (no labels / briefs on the canvas). */
    static constexpr float kNodeW = 56.0f;
    static constexpr float kNodeH = 56.0f;
    static constexpr float kPortR = 7.0f;
    static constexpr float kStartSize = 88.0f;

    void clearGraph();
    void beginPlaceFromPalette(DialogNodeKind kind);
    void ensureIconsLoaded();
    void unloadIcons();
    Texture2D iconForKind(DialogNodeKind kind) const;
    void handleInput(Rectangle bounds, bool allowInteraction);
    void draw(Rectangle bounds);
    void drawContextMenu();

    /**
     * Rebuild the flowchart from conversations.json speakPhases for the current
     * tree selection (scene / actor / phase / choice). Editor-local layout;
     * does not rewrite JSON yet.
     */
    void migrateFromTreeSelection(const std::string& treeKey, const std::string& sceneId);

    int selectedOrStartId() const { return selectedNodeId; }

    DialogFlowNode* findNode(int id);
    const DialogFlowNode* findNode(int id) const;

    /** Scope key of the last successful migrate (avoid redundant rebuilds). */
    std::string migratedScope;

private:
    Vector2 worldFromScreen(Vector2 screen) const;
    Vector2 screenFromWorld(Vector2 world) const;
    Rectangle startCardWorld() const;
    Rectangle nodeCardWorld(const DialogFlowNode& n) const;
    Vector2 parentPortWorld(const DialogFlowNode& n) const;
    Vector2 childPortWorld(int nodeId, int childIndex) const; // nodeId 0 = Start
    Vector2 startChildPortWorld() const;

    void drawCompassRose(Vector2 center, float radius) const;
    void drawKindIcon(DialogNodeKind kind, Rectangle dest) const;
    bool hitPort(
        Vector2 screen,
        int& outNodeId,
        int& outChildIndex,
        bool& outIsParent) const;
    int hitNodeCard(Vector2 screen) const;
    void tryFinishWire(Vector2 screen);

    void openContextMenu(int nodeId, Vector2 screen);
    void closeContextMenu();
    void buildContextMenuItems(const DialogFlowNode& n);
    void applyContextMenuAction(int action);
    void openVoiceMenu(int contextRow);
    void closeVoiceMenu();
    bool handleVoiceMenuClick(Vector2 mouse);
    void drawVoiceMenu() const;
    void layoutVoiceMenu();
    void openParchmentForField(
        std::string* field,
        bool tts,
        const std::string& label,
        const std::string& companionPlain = {},
        const std::string& bakeVoiceId = {},
        const std::string& bakeAudioRelPath = {},
        std::string* bakeAudioBind = nullptr,
        std::vector<std::string>* bakeAudioSegmentsBind = nullptr);
    void deleteNode(int id);
    static std::string formatDefaultVoiceLabel(const std::string& voiceId);
    /** Write TTS text / voice / audio path back to conversations.json when possible. */
    void persistNodeTtsToJson(DialogFlowNode& n);
    std::string sceneDefaultVoice() const;

    /** Progressive edge pan: 64px band slow, 16px band faster. */
    void applyEdgeAutoPan(Rectangle bounds);
    void contentScrollLimits(float& minX, float& minY, float& maxX, float& maxY) const;
    void clampScroll();

    /**
     * Layered tree layout after migrate (or manual mess): non-overlapping
     * columns per depth, barycenter ordering to cut crossings.
     */
    void relayoutGraph();

    int allocNode(DialogNodeKind kind, float x, float y);
    void addEdge(int fromId, int fromChild, int toId);
    int migrateActorLine(
        const std::string& text,
        const std::string& tts,
        const std::string& pointer,
        float x,
        float y,
        const std::string& voice = {});
    int migratePlayerLine(
        const std::string& label,
        const std::string& pointer,
        const std::string& choiceId,
        float x,
        float y,
        const std::string& voice = {});
    /** Wire parent→child using next free child port; spill to chain node if >5. */
    void connectWithPorts(int fromId, int toId);
    /**
     * Migrate one choice. When skipPlayerNode is true, do not emit a Player
     * Dialog (used under an Event money gate that already owns the shared ask).
     */
    void migrateChoiceTree(
        const nlohmann::json& choice,
        const std::string& pointer,
        int parentId,
        float x,
        float y,
        int depth,
        std::map<std::string, int>& phaseIntroIds,
        std::map<std::string, int>& choicePlayerIds,
        std::vector<std::pair<int, std::string>>& deferredStartPhase,
        std::vector<std::pair<int, std::string>>& deferredResume,
        bool skipPlayerNode = false);
    /**
     * Migrate a choices[] array. Complementary availability siblings become
     * Event gates (money same-label pairs, tab can-pay vs can't-pay). Solo
     * requiresMoney / tab / room gates get a single-branch Event so the
     * condition is visible on the flowchart.
     */
    void migrateChoicesArray(
        const nlohmann::json& choices,
        const std::string& arrayPointer,
        int parentId,
        float originX,
        float originY,
        int depth,
        std::map<std::string, int>& phaseIntroIds,
        std::map<std::string, int>& choicePlayerIds,
        std::vector<std::pair<int, std::string>>& deferredStartPhase,
        std::vector<std::pair<int, std::string>>& deferredResume);
    /** Shared-label money fork: one Player ask → Event → pass/fail bodies. */
    void migrateCollapsedMoneyGatePair(
        const nlohmann::json& cashChoice,
        const std::string& cashPointer,
        const nlohmann::json& insuffChoice,
        const std::string& insuffPointer,
        float amount,
        int parentId,
        float x,
        float y,
        int depth,
        std::map<std::string, int>& phaseIntroIds,
        std::map<std::string, int>& choicePlayerIds,
        std::vector<std::pair<int, std::string>>& deferredStartPhase,
        std::vector<std::pair<int, std::string>>& deferredResume);
    /** Different-label complementary pair: Event → two full player branches. */
    void migrateBranchingAvailabilityPair(
        const nlohmann::json& passChoice,
        const std::string& passPointer,
        const nlohmann::json& failChoice,
        const std::string& failPointer,
        const std::string& eventId,
        int parentId,
        float x,
        float y,
        int depth,
        std::map<std::string, int>& phaseIntroIds,
        std::map<std::string, int>& choicePlayerIds,
        std::vector<std::pair<int, std::string>>& deferredStartPhase,
        std::vector<std::pair<int, std::string>>& deferredResume);
    /** Unpaired gated choice: Player → Event (pass only) → body. */
    void migrateChoiceWithSoloAvailabilityGate(
        const nlohmann::json& choice,
        const std::string& pointer,
        const std::string& eventId,
        int parentId,
        float x,
        float y,
        int depth,
        std::map<std::string, int>& phaseIntroIds,
        std::map<std::string, int>& choicePlayerIds,
        std::vector<std::pair<int, std::string>>& deferredStartPhase,
        std::vector<std::pair<int, std::string>>& deferredResume);
    void migratePhase(
        const nlohmann::json& phase,
        size_t phaseIndex,
        const std::string& sceneId,
        bool connectFromStart,
        float originX,
        float originY,
        std::map<std::string, int>& phaseIntroIds,
        std::map<std::string, int>& choicePlayerIds,
        std::vector<std::pair<int, std::string>>& deferredStartPhase,
        std::vector<std::pair<int, std::string>>& deferredResume);
};

} // namespace timberline_editor

#endif
