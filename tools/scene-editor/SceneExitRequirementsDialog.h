/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author exitRequirements for a compass edge: gates, block badge, blocked VO.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_EXIT_REQUIREMENTS_DIALOG_H
#define TIMBERLINE_SCENE_EXIT_REQUIREMENTS_DIALOG_H

#include "DocumentWorkspace.h"
#include "FullscreenParchmentEditor.h"
#include "SceneGraphModel.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct ExitBlockedVariantEdit
{
    std::string when;
    std::string details;
    std::string ttsText;
    std::string ttsVoice = "leo";
    std::string ttsAudio;
};

struct SceneExitRequirementsDialog
{
    DocumentWorkspace* docs = nullptr;
    SceneGraphModel* graph = nullptr;
    FullscreenParchmentEditor* parchment = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    /** Active side being edited (from → direction → to). */
    std::string sceneId;
    std::string direction;
    std::string toSceneId;

    /**
     * Wire endpoints. Side 0 is the opened link; side 1 is the reciprocal
     * return path when one exists (slider flips between them).
     */
    std::string sideFrom[2];
    std::string sideDir[2];
    std::string sideTo[2];
    bool reverseAvailable = false;
    /** false = side 0, true = side 1 */
    bool editingReverse = false;

    bool requiresLightSource = false;
    bool requiresRoomPurchasedToday = false;
    std::string requiresInventoryItem;
    std::string requiresStoryFlag;
    /** auto | light | lock | gear */
    std::string blockBadge = "auto";
    std::string blockedDetails;
    std::string blockedTtsText;
    std::string blockedTtsVoice = "leo";
    std::string blockedTtsAudio;

    /** Conditional blocked VO bags (#42 P3). First matching when wins. */
    std::vector<ExitBlockedVariantEdit> blockedVariants;
    int selectedVariant = -1;

    int focusField = 0; // 0 item, 1 flag, 2 details, 3 tts, 4 api, 5–7 variant
    float scrollY = 0.0f;
    float lastContentH = 0.0f;
    Rectangle lastScrollClip{0, 0, 0, 0};

    bool fieldContextOpen = false;
    int fieldContextTarget = -1;
    Rectangle fieldContextRect{0, 0, 0, 0};

    bool voiceMenuOpen = false;
    Rectangle voiceBtnRect{0, 0, 0, 0};
    Rectangle voiceMenuRect{0, 0, 0, 0};
    Rectangle directionSliderRect{0, 0, 0, 0};

    /** Inventory-item id autocomplete (#47). */
    bool itemSuggestOpen = false;
    float itemSuggestScroll = 0.0f;
    Rectangle itemSuggestRect{0, 0, 0, 0};
    Rectangle itemFieldRect{0, 0, 0, 0};

    std::string sessionApiKey;
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
    bool generateResultPending = false;
    /** 0=none, 1=Generate TTS dialog, 2=Generate Voice */
    int generateKind = 0;
    std::string pendingTtsJobsPath;
    std::thread generateThread;

    std::function<void()> onSaved;

    void openForExit(
        const std::string& fromSceneId,
        const std::string& dir,
        const std::string& toId);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void resolveWireSides(
        const std::string& fromSceneId,
        const std::string& dir,
        const std::string& toId);
    void applyActiveSide();
    bool setEditingReverse(bool reverse);
    void loadFromScene();
    bool applyChanges();
    void clearRequirement();
    void typeIntoFocusedField();
    std::string* focusedString();
    void cycleBadge();
    void suggestBadgeFromGates();
    /** Token being typed in the comma-separated inventory field (for autocomplete). */
    std::string inventoryItemTokenPrefix() const;
    void applyInventoryItemSuggestion(const std::string& itemId);
    std::vector<std::string> inventoryItemSuggestions(int maxCount) const;
    std::string defaultBlockedAudioPath() const;
    std::string defaultVariantAudioPath(int index) const;
    bool blockedAudioExists() const;
    void addBlockedVariant();
    void removeSelectedVariant();
    void stopPreviewVoice();
    void updatePreviewVoice();
    void startPreviewVoice();
    void startTtsDialogGenerate();
    void startVoiceRefresh();
    void pollGenerateResult();
    std::string effectiveApiKey() const;
};

} // namespace timberline_editor

#endif
