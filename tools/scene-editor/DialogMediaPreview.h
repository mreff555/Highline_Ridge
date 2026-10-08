/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-right media preview.
 * Two columns: scene image (left) | beds + Play TTS (right).
 ******************************************************************************/

#ifndef TIMBERLINE_DIALOG_MEDIA_PREVIEW_H
#define TIMBERLINE_DIALOG_MEDIA_PREVIEW_H

#include "DialogFlowCanvas.h"
#include "DocumentWorkspace.h"
#include "ThumbnailCache.h"

#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct DialogMediaPreview
{
    DocumentWorkspace* docs = nullptr;
    DialogFlowCanvas* flow = nullptr;
    ThumbnailCache* thumbnails = nullptr;
    std::string* selectionSceneId = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    Music previewMusic{};
    bool previewMusicLoaded = false;
    bool previewMusicPlaying = false;
    std::string previewMusicPath;
    std::string previewMusicTemp;

    Music previewAmbient{};
    bool previewAmbientLoaded = false;
    bool previewAmbientPlaying = false;
    std::string previewAmbientPath;
    std::string previewAmbientTemp;

    Music previewTts{};
    bool previewTtsLoaded = false;
    bool previewTtsPlaying = false;
    std::string previewTtsPath;
    std::string previewTtsTemp;
    std::string previewTtsRel;
    long previewTtsModTime = 0;
    std::vector<std::string> previewTtsQueue;
    size_t previewTtsQueueIndex = 0;

    void unloadAudio();
    void unloadTts();
    void updateStreams();
    void handleInput(Rectangle bounds, bool allowInteraction);
    void draw(Rectangle bounds);

private:
    void ensureSceneAudioLoaded();
    void ensureSelectedTtsLoaded();
    bool ttsAudioFileExists(const std::string& relPath) const;
    std::vector<std::string> resolveSelectedDialogTtsQueue() const;
    std::string resolveSelectedDialogTtsRel() const;
    void toggleBed(Music& music, bool loaded, bool& playing, float volume);
    void toggleTtsPreview();
    bool loadMusicPath(
        const std::string& path,
        Music& out,
        bool& loaded,
        std::string& tempOut,
        bool looping);
};

} // namespace timberline_editor

#endif
