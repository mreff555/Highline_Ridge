/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author a Conversations Actor Dialog flowchart node (text/TTS, resume,
 * player-response ports, status, availability). Dual-writes conversations.json.
 ******************************************************************************/

#ifndef TIMBERLINE_ACTOR_DIALOG_AUTHORING_DIALOG_H
#define TIMBERLINE_ACTOR_DIALOG_AUTHORING_DIALOG_H

#include "DialogFlowCanvas.h"
#include "DocumentWorkspace.h"
#include "EditorApiKeys.h"
#include "FullscreenParchmentEditor.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct ActorDialogBagEdit
{
    std::string text;
    std::string ttsText;
    std::string ttsVoice;
    std::string ttsAudio;
    std::vector<std::string> ttsAudioSegments;
};

struct ActorDialogAuthoringDialog
{
    DocumentWorkspace* docs = nullptr;
    DialogFlowCanvas* flow = nullptr;
    FullscreenParchmentEditor* parchment = nullptr;
    EditorApiKeys* sessionKeys = nullptr;
    Font uiFont{};
    Font uiFontBold{};
    bool open = false;
    std::function<void()> onSaved;

    void openForActorDialog(const std::string& sceneId, DialogFlowNode* node);
    void closeDialog();
    bool blocksInput() const { return open; }
    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

    ActorDialogAuthoringDialog() = default;
    ~ActorDialogAuthoringDialog();
    ActorDialogAuthoringDialog(const ActorDialogAuthoringDialog&) = delete;
    ActorDialogAuthoringDialog& operator=(const ActorDialogAuthoringDialog&) = delete;

private:
    std::string sceneId;
    DialogFlowNode* node = nullptr;
    std::string loadedJsonPointer;
    std::string loadedLineId;
    /** intro | text | response — which JSON string this node's on-screen text maps to. */
    std::string textFieldKey;
    bool isOpener = false;

    std::string lineId;
    std::string actorId;
    std::string actorName;
    ActorDialogBagEdit bags[2];
    int selectedBag = 0;

    bool playerResponsesEnabled = false;

    float statusHealth = 0.0f;
    float statusEnergy = 0.0f;
    float statusResolve = 0.0f;
    float statusLucidity = 0.0f;
    float statusCharisma = 0.0f;
    float statusMoney = 0.0f;
    std::string statusActor;
    int statusActorOpinion = 0;
    float statusActorTab = 0.0f;
    std::string statusKey;
    std::string statusOnZeroLucidity;

    float requiresMoney = 0.0f;
    float requiresInsufficientMoney = 0.0f;
    bool requiresPositiveActorTab = false;
    bool requiresPayActorTabInFull = false;
    bool requiresInsufficientForActorTab = false;
    bool requiresSaloonRoomAvailable = false;
    std::string tabActor;
    bool requiresLightSource = false;
    std::string requiresInventoryItem;
    std::string requiresFlag;
    std::string requiresPhase;

    int minDay = 0;
    int maxDay = 0;
    float requiresLucidityBelow = 0.0f;
    float requiresLucidityAbove = 0.0f;
    bool requiresRecentCollapse = false;
    bool requiresRecentSleep = false;
    std::string requiresDaysSinceFlag;
    int requiresDaysSinceMin = 0;
    int requiresDaysSinceMax = 0;
    std::string opinionActor;
    bool hasActorOpinionAtLeast = false;
    int requiresActorOpinionAtLeast = 0;
    bool hasActorOpinionAtMost = false;
    int requiresActorOpinionAtMost = 0;

    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    int focusField = 0;
    std::string focusDraft;
    float scrollY = 0.0f;
    float lastContentH = 0.0f;
    Rectangle lastScrollClip{0, 0, 0, 0};

    bool voiceMenuOpen = false;
    Rectangle voiceBtnRect{0, 0, 0, 0};
    Rectangle voiceMenuRect{0, 0, 0, 0};
    float voiceMenuScroll = 0.0f;
    static constexpr int kVoiceMenuVisibleRows = 10;

    bool itemSuggestOpen = false;
    Rectangle itemSuggestRect{0, 0, 0, 0};
    Rectangle itemFieldRect{0, 0, 0, 0};

    std::string status;
    std::string error;

    Music previewVoice{};
    bool previewVoiceLoaded = false;
    bool previewVoicePlaying = false;
    std::string previewVoiceTempFile;

    std::atomic<bool> generateBusy{false};
    std::atomic<bool> generateCancel{false};
    std::mutex generateMutex;
    std::string generateResultStatus;
    std::string generateResultPath;
    std::vector<std::string> generateResultSegments;
    bool generateResultPending = false;
    /** 0=none, 1=Generate TTS dialog, 2=Generate voice */
    int generateKind = 0;
    int generateBag = 0;
    std::string pendingTtsJobsPath;
    std::thread generateThread;

    void resetWorkingState();
    void loadFromNodeAndJson();
    void copyNodeToDialog(const DialogFlowNode& n);
    void copyDialogToNode(DialogFlowNode& n) const;
    bool applyChanges();
    bool applyRename();
    void typeIntoFocusedField();
    std::string* focusedString();
    bool focusedIsNumeric() const;
    void beginNumericFocus(int field, const std::string& initial);
    void commitNumericFocus();
    ActorDialogBagEdit& activeBag();
    const ActorDialogBagEdit& activeBag() const;
    std::string defaultBagAudioPath(int bagIndex) const;
    std::string bagAudioPath(int bagIndex) const;
    bool bagAudioExists(int bagIndex) const;
    std::string inventoryItemTokenPrefix() const;
    void applyInventoryItemSuggestion(const std::string& itemId);
    std::vector<std::string> inventoryItemSuggestions(int maxCount) const;
    void stopPreviewVoice();
    void updatePreviewVoice();
    void startPreviewVoice();
    void startTtsDialogGenerate();
    void startVoiceGenerate();
    void pollGenerateResult();
    void joinGenerateThread();
    std::string effectiveApiKey() const;
    void openParchmentForBag(bool tts);
    nlohmann::json* resolveBagObject();
    const nlohmann::json* resolveBagObject() const;
    bool lineIdIsUnique(const std::string& candidate) const;
    void rewritePhaseRefs(nlohmann::json& node, const std::string& oldId, const std::string& newId);
    void applyPlayerResponsesToggle(bool enabled);
    void addPlayerResponseRow();
};

} // namespace timberline_editor

#endif
