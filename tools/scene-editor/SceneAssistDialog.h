/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Scenes-tab AI Assist: regenerate image / ambient / music with Accept/Revert.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_ASSIST_DIALOG_H
#define TIMBERLINE_SCENE_ASSIST_DIALOG_H

#include "DocumentWorkspace.h"
#include "SceneAuthoring.h"
#include "ThumbnailCache.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <raylib.h>

namespace timberline_editor
{

struct SceneAssistDialog
{
    DocumentWorkspace* docs = nullptr;
    ThumbnailCache* thumbnails = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    float descScrollY = 0.0f;

    SceneAuthoringPayload payload{};
    std::string sessionApiKey;
    std::string status;
    std::string error;
    int focusField = 0; // 0=api key

    // 0=idle chooser, 1=image, 2=ambient, 3=music
    int generateTarget = 0;
    std::atomic<bool> generateBusy{false};
    std::mutex generateMutex;
    std::string generateResultStatus;
    bool generateResultPending = false;
    std::thread generateThread;

    // Pending Accept/Revert preview (temp path written by AI).
    bool previewPending = false;
    int previewTarget = 0;
    std::string previewRelPath;
    std::string liveRelPath;
    Texture2D previewTexture{};
    bool previewTextureLoaded = false;
    Music previewMusic{};
    bool previewMusicLoaded = false;
    std::string previewMusicTempFile;
    bool previewMusicPlaying = false;
    bool audioDeviceReady = false;

    std::function<void()> onAccepted; // e.g. invalidate thumbs / reload transport

    void openForScene(const std::string& sceneId);
    void closeDialog();
    bool blocksInput() const { return open; }

    /** While previewPending, bottom pane may show/play these instead of live. */
    bool hasPreviewOverride(const std::string& sceneId) const;
    std::string overrideImagePath(const std::string& sceneId) const;
    std::string overrideAmbientPath(const std::string& sceneId) const;
    std::string overrideMusicPath(const std::string& sceneId) const;
    Texture2D* overrideImageTexture(const std::string& sceneId);

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);
    void pollGenerateResult();
    void updateAudio();

private:
    void ensureAudio();
    void unloadPreviewAssets();
    void startGenerate(int target);
    void acceptPreview();
    void revertPreview();
    void typeIntoFocusedField();
    bool loadPreviewTexture(const std::string& relPath);
    bool loadPreviewMusic(const std::string& relPath);
    void stopPreviewMusic();
};

} // namespace timberline_editor

#endif
