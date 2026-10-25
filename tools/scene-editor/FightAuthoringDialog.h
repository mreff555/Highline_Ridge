/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author attackEncounters for a Fight (Attack) flowchart node.
 ******************************************************************************/

#ifndef TIMBERLINE_FIGHT_AUTHORING_DIALOG_H
#define TIMBERLINE_FIGHT_AUTHORING_DIALOG_H

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

struct FightBagEdit
{
    std::string text;
    std::string ttsText;
    std::string ttsVoice = "leo";
    std::string ttsAudio;
    std::vector<std::string> ttsAudioSegments;
};

/** Copy parseAttackEncounter-shaped JSON onto a Fight node (migrate / open). */
void fillFightNodeFromEncounterJson(
    DialogFlowNode& node,
    const nlohmann::json& encounter);

struct FightAuthoringDialog
{
    DocumentWorkspace* docs = nullptr;
    FullscreenParchmentEditor* parchment = nullptr;
    EditorApiKeys* sessionKeys = nullptr;
    Font uiFont{};
    Font uiFontBold{};
    bool open = false;
    std::function<void()> onSaved;

    void openForFight(const std::string& sceneId, DialogFlowNode* node);
    void closeDialog();
    bool blocksInput() const { return open; }
    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

    FightAuthoringDialog() = default;
    ~FightAuthoringDialog();
    FightAuthoringDialog(const FightAuthoringDialog&) = delete;
    FightAuthoringDialog& operator=(const FightAuthoringDialog&) = delete;

private:
    std::string sceneId;
    DialogFlowNode* node = nullptr;
    std::string loadedEncounterId;

    std::string encounterId;
    std::string combatantId;
    FightBagEdit bags[3];
    int selectedBag = 0;

    int actorEdgeHealth = 0;
    int actorEdgeEnergy = 0;
    int actorEdgeResolve = 0;
    int actorEdgeLucidity = 0;
    int actorEdgeCharisma = 0;

    bool cannotLose = false;
    bool playerDeath = false;
    bool lossHealth = false;
    bool lossEnergy = false;
    bool lossResolve = false;
    bool lossLucidity = false;
    bool lossCharisma = false;
    float lossHealthPct = 50.0f;
    float lossEnergyPct = 50.0f;
    float lossResolvePct = 50.0f;
    float lossLucidityPct = 50.0f;
    float lossCharismaPct = 50.0f;

    bool reputationChange = false;
    int reputationDelta = 0;

    float requiresMoney = 0.0f;
    float requiresInsufficientMoney = 0.0f;
    bool requiresPositiveActorTab = false;
    bool requiresPayActorTabInFull = false;
    bool requiresInsufficientForActorTab = false;
    bool requiresSaloonRoomAvailable = false;
    std::string tabActor;
    bool requiresLightSource = false;
    std::string requiresInventoryItem;
    std::string requiresStoryFlag;

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
    void typeIntoFocusedField();
    std::string* focusedString();
    bool focusedIsNumeric() const;
    void beginNumericFocus(int field, const std::string& initial);
    void commitNumericFocus();
    FightBagEdit& activeBag();
    const FightBagEdit& activeBag() const;
    std::string effectiveEncounterId() const;
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
};

} // namespace timberline_editor

#endif
