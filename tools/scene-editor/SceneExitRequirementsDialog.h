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

#include <raylib.h>

namespace timberline_editor
{

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

    std::string sceneId;
    std::string direction;
    std::string toSceneId;

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

    int focusField = 0; // 0 item, 1 flag, 2 details, 3 tts, 4 api key
    float scrollY = 0.0f;
    float lastContentH = 0.0f;

    bool fieldContextOpen = false;
    int fieldContextTarget = -1;
    Rectangle fieldContextRect{0, 0, 0, 0};

    bool voiceMenuOpen = false;
    Rectangle voiceBtnRect{0, 0, 0, 0};
    Rectangle voiceMenuRect{0, 0, 0, 0};

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
    void loadFromScene();
    bool applyChanges();
    void clearRequirement();
    void typeIntoFocusedField();
    std::string* focusedString();
    void cycleBadge();
    void suggestBadgeFromGates();
    std::string defaultBlockedAudioPath() const;
    bool blockedAudioExists() const;
    void stopPreviewVoice();
    void updatePreviewVoice();
    void startPreviewVoice();
    void startVoiceRefresh();
    void pollGenerateResult();
    std::string effectiveApiKey() const;
};

} // namespace timberline_editor

#endif
