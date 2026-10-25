/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-right media preview.
 ******************************************************************************/

#include "DialogMediaPreview.h"
#include "EditorAudio.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "EditorTypes.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

namespace timberline_editor
{

namespace
{
constexpr float kColGap = 8.0f;
}

void DialogMediaPreview::unloadTts()
{
    if (previewTtsLoaded && IsMusicStreamPlaying(previewTts))
        StopMusicStream(previewTts);
    previewTtsPlaying = false;
    if (previewTtsLoaded)
    {
        UnloadMusicStream(previewTts);
        previewTts = {};
        previewTtsLoaded = false;
    }
    if (!previewTtsTemp.empty())
    {
        std::remove(previewTtsTemp.c_str());
        previewTtsTemp.clear();
    }
    previewTtsPath.clear();
    previewTtsRel.clear();
    previewTtsModTime = 0;
    previewTtsQueue.clear();
    previewTtsQueueIndex = 0;
}

void DialogMediaPreview::unloadAudio()
{
    if (previewMusicLoaded && IsMusicStreamPlaying(previewMusic))
        StopMusicStream(previewMusic);
    previewMusicPlaying = false;
    if (previewMusicLoaded)
    {
        UnloadMusicStream(previewMusic);
        previewMusic = {};
        previewMusicLoaded = false;
    }
    if (!previewMusicTemp.empty())
    {
        std::remove(previewMusicTemp.c_str());
        previewMusicTemp.clear();
    }
    previewMusicPath.clear();

    if (previewAmbientLoaded && IsMusicStreamPlaying(previewAmbient))
        StopMusicStream(previewAmbient);
    previewAmbientPlaying = false;
    if (previewAmbientLoaded)
    {
        UnloadMusicStream(previewAmbient);
        previewAmbient = {};
        previewAmbientLoaded = false;
    }
    if (!previewAmbientTemp.empty())
    {
        std::remove(previewAmbientTemp.c_str());
        previewAmbientTemp.clear();
    }
    previewAmbientPath.clear();

    unloadTts();
}

void DialogMediaPreview::updateStreams()
{
    if (previewMusicLoaded && previewMusicPlaying)
        UpdateMusicStream(previewMusic);
    if (previewAmbientLoaded && previewAmbientPlaying)
        UpdateMusicStream(previewAmbient);
    if (previewTtsLoaded && previewTtsPlaying)
    {
        UpdateMusicStream(previewTts);
        if (!IsMusicStreamPlaying(previewTts))
        {
            // Advance multi-voice segment queue (leo → helios, etc.).
            if (previewTtsQueueIndex + 1 < previewTtsQueue.size())
            {
                const size_t next = previewTtsQueueIndex + 1;
                const std::string nextRel = previewTtsQueue[next];
                if (previewTtsLoaded)
                {
                    UnloadMusicStream(previewTts);
                    previewTts = {};
                    previewTtsLoaded = false;
                }
                if (!previewTtsTemp.empty())
                {
                    std::remove(previewTtsTemp.c_str());
                    previewTtsTemp.clear();
                }
                if (loadMusicPath(
                        nextRel, previewTts, previewTtsLoaded, previewTtsTemp, false))
                {
                    previewTtsQueueIndex = next;
                    previewTtsRel = nextRel;
                    previewTtsPath = nextRel;
                    SeekMusicStream(previewTts, 0.0f);
                    SetMusicVolume(previewTts, 1.0f);
                    PlayMusicStream(previewTts);
                    previewTtsPlaying = true;
                }
                else
                    previewTtsPlaying = false;
            }
            else
                previewTtsPlaying = false;
        }
    }
}

bool DialogMediaPreview::loadMusicPath(
    const std::string& path,
    Music& out,
    bool& loaded,
    std::string& tempOut,
    bool looping)
{
    loaded = false;
    if (!editorAudioDeviceReady() || docs == nullptr || path.empty())
        return false;

    using timberline_engine::buildAssetSearchPaths;
    using timberline_engine::compressedAssetPath;
    using timberline_engine::loadAssetBytesFromFile;
    using timberline_engine::pathJoin;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, path);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = path;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }

    for (const std::string& candidate : candidates)
    {
        if (FileExists(candidate.c_str()))
        {
            out = LoadMusicStream(candidate.c_str());
            if (IsMusicValid(out))
            {
                out.looping = looping;
                loaded = true;
                return true;
            }
        }
        const std::string compressed = compressedAssetPath(candidate);
        if (FileExists(compressed.c_str()))
        {
            std::vector<unsigned char> bytes;
            if (loadAssetBytesFromFile(compressed, bytes) && !bytes.empty())
            {
                std::string fileType = ".mp3";
                const size_t dot = candidate.find_last_of('.');
                if (dot != std::string::npos)
                    fileType = candidate.substr(dot);
                const std::string tmp = pathJoin(
                    GetApplicationDirectory() ? GetApplicationDirectory() : ".",
                    std::string("editor_dialog_bed_")
                        + (looping ? (tempOut.empty() ? "m" : "a") : "tts")
                        + fileType);
                std::ofstream fout(tmp.c_str(), std::ios::binary);
                if (fout)
                {
                    fout.write(
                        reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                    fout.close();
                    out = LoadMusicStream(tmp.c_str());
                    if (IsMusicValid(out))
                    {
                        out.looping = looping;
                        tempOut = tmp;
                        loaded = true;
                        return true;
                    }
                    std::remove(tmp.c_str());
                }
            }
        }
    }
    return false;
}

bool DialogMediaPreview::ttsAudioFileExists(const std::string& relPath) const
{
    if (relPath.empty() || docs == nullptr)
        return false;
    using timberline_engine::buildAssetSearchPaths;
    using timberline_engine::compressedAssetPath;
    using timberline_engine::pathJoin;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    for (const std::string& candidate : buildAssetSearchPaths(assetRoot, relPath))
    {
        if (FileExists(candidate.c_str())
            || FileExists(compressedAssetPath(candidate).c_str()))
            return true;
    }
    if (!docs->resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        const std::string underRes = pathJoin(docs->resourceDir, stripped);
        if (FileExists(underRes.c_str())
            || FileExists(compressedAssetPath(underRes).c_str()))
            return true;
    }
    return false;
}

std::vector<std::string> DialogMediaPreview::resolveSelectedDialogTtsQueue() const
{
    std::vector<std::string> queue;
    if (flow == nullptr || docs == nullptr)
        return queue;
    const int id = flow->selectedNodeId;
    if (id <= 0)
        return queue;
    const DialogFlowNode* n = flow->findNode(id);
    if (n == nullptr)
        return queue;
    if (n->kind != DialogNodeKind::ActorDialog
        && n->kind != DialogNodeKind::PlayerDialog)
        return queue;

    auto appendExisting = [&](const std::vector<std::string>& paths) {
        for (const std::string& p : paths)
        {
            if (!p.empty() && ttsAudioFileExists(p))
                queue.push_back(p);
        }
    };

    // 1) Node-local multi-voice segments from migrate / Generate voice.
    if (n->dialogTtsAudioSegments.size() > 1)
    {
        appendExisting(n->dialogTtsAudioSegments);
        if (!queue.empty())
            return queue;
    }
    if (!n->dialogTtsAudio.empty() && ttsAudioFileExists(n->dialogTtsAudio))
    {
        queue.push_back(n->dialogTtsAudio);
        return queue;
    }

    // 2) Authored bag on conversations.json (parent phase for leaf pointers).
    if (!n->jsonPointer.empty())
    {
        const bool resumeLeaf = n->jsonPointer.size() >= 12
            && n->jsonPointer.compare(
                   n->jsonPointer.size() - 12, 12, "/resumeIntro")
                == 0;
        const bool introLeaf = n->jsonPointer.size() >= 6
            && n->jsonPointer.compare(n->jsonPointer.size() - 6, 6, "/intro")
                == 0;
        const bool textLeaf = n->jsonPointer.size() >= 5
            && n->jsonPointer.compare(n->jsonPointer.size() - 5, 5, "/text")
                == 0;

        std::string bagPtr = n->jsonPointer;
        if (resumeLeaf || introLeaf || textLeaf)
        {
            const size_t slash = n->jsonPointer.rfind('/');
            if (slash != std::string::npos)
                bagPtr = n->jsonPointer.substr(0, slash);
        }

        const nlohmann::json* obj = docs->conversationJsonAt(bagPtr);
        if (obj != nullptr && obj->is_object())
        {
            if (!resumeLeaf && obj->contains("ttsAudioSegments")
                && (*obj)["ttsAudioSegments"].is_array())
            {
                std::vector<std::string> segs;
                for (const auto& s : (*obj)["ttsAudioSegments"])
                {
                    if (s.is_string())
                        segs.push_back(s.get<std::string>());
                }
                appendExisting(segs);
                if (!queue.empty())
                    return queue;
            }
            const std::string audio = resumeLeaf
                ? obj->value("resumeTtsAudio", std::string())
                : obj->value("ttsAudio", std::string());
            if (!audio.empty() && ttsAudioFileExists(audio))
            {
                queue.push_back(audio);
                return queue;
            }
        }
    }

    // 3) Last-resort parchment bake when no bag path is wired yet.
    const std::string parchmentBake = "resources/.authoring/parchment_voice.mp3";
    if (ttsAudioFileExists(parchmentBake) && !n->dialogTts.empty())
        queue.push_back(parchmentBake);
    return queue;
}

std::string DialogMediaPreview::resolveSelectedDialogTtsRel() const
{
    const std::vector<std::string> queue = resolveSelectedDialogTtsQueue();
    return queue.empty() ? std::string() : queue.front();
}

void DialogMediaPreview::ensureSceneAudioLoaded()
{
    if (docs == nullptr || selectionSceneId == nullptr || selectionSceneId->empty())
    {
        if (previewMusicLoaded || previewAmbientLoaded)
        {
            // Keep TTS; only clear beds when scene is gone.
            if (previewMusicLoaded && IsMusicStreamPlaying(previewMusic))
                StopMusicStream(previewMusic);
            previewMusicPlaying = false;
            if (previewMusicLoaded)
            {
                UnloadMusicStream(previewMusic);
                previewMusic = {};
                previewMusicLoaded = false;
            }
            if (!previewMusicTemp.empty())
            {
                std::remove(previewMusicTemp.c_str());
                previewMusicTemp.clear();
            }
            previewMusicPath.clear();

            if (previewAmbientLoaded && IsMusicStreamPlaying(previewAmbient))
                StopMusicStream(previewAmbient);
            previewAmbientPlaying = false;
            if (previewAmbientLoaded)
            {
                UnloadMusicStream(previewAmbient);
                previewAmbient = {};
                previewAmbientLoaded = false;
            }
            if (!previewAmbientTemp.empty())
            {
                std::remove(previewAmbientTemp.c_str());
                previewAmbientTemp.clear();
            }
            previewAmbientPath.clear();
        }
        return;
    }
    if (!docs->scenes.hasScene(*selectionSceneId))
        return;

    const std::string musicPath = docs->scenes.getSceneMusicPath(*selectionSceneId);
    const std::string ambientPath = docs->scenes.getSceneAmbientPath(*selectionSceneId);

    if (musicPath != previewMusicPath)
    {
        if (previewMusicLoaded)
        {
            if (IsMusicStreamPlaying(previewMusic))
                StopMusicStream(previewMusic);
            UnloadMusicStream(previewMusic);
            previewMusic = {};
            previewMusicLoaded = false;
        }
        if (!previewMusicTemp.empty())
        {
            std::remove(previewMusicTemp.c_str());
            previewMusicTemp.clear();
        }
        previewMusicPlaying = false;
        previewMusicPath = musicPath;
        if (!musicPath.empty())
            loadMusicPath(
                musicPath, previewMusic, previewMusicLoaded, previewMusicTemp, true);
        if (!previewMusicLoaded)
            previewMusicPath.clear();
    }

    if (ambientPath != previewAmbientPath)
    {
        if (previewAmbientLoaded)
        {
            if (IsMusicStreamPlaying(previewAmbient))
                StopMusicStream(previewAmbient);
            UnloadMusicStream(previewAmbient);
            previewAmbient = {};
            previewAmbientLoaded = false;
        }
        if (!previewAmbientTemp.empty())
        {
            std::remove(previewAmbientTemp.c_str());
            previewAmbientTemp.clear();
        }
        previewAmbientPlaying = false;
        previewAmbientPath = ambientPath;
        if (!ambientPath.empty())
            loadMusicPath(
                ambientPath,
                previewAmbient,
                previewAmbientLoaded,
                previewAmbientTemp,
                true);
        if (!previewAmbientLoaded)
            previewAmbientPath.clear();
    }
}

void DialogMediaPreview::ensureSelectedTtsLoaded()
{
    const std::vector<std::string> queue = resolveSelectedDialogTtsQueue();
    if (queue.empty())
    {
        if (!previewTtsRel.empty() || previewTtsLoaded || !previewTtsQueue.empty())
            unloadTts();
        return;
    }

    const std::string rel = queue.front();
    using timberline_engine::buildAssetSearchPaths;
    using timberline_engine::pathJoin;
    const std::string assetRoot =
        docs != nullptr && !docs->assetRoot.empty() ? docs->assetRoot : ".";

    long plainMod = 0;
    bool plainExists = false;
    for (const std::string& candidate : buildAssetSearchPaths(assetRoot, rel))
    {
        if (FileExists(candidate.c_str()))
        {
            plainExists = true;
            plainMod = GetFileModTime(candidate.c_str());
            break;
        }
    }
    if (!plainExists && docs != nullptr && !docs->resourceDir.empty())
    {
        std::string stripped = rel;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        const std::string underRes = pathJoin(docs->resourceDir, stripped);
        if (FileExists(underRes.c_str()))
        {
            plainExists = true;
            plainMod = GetFileModTime(underRes.c_str());
        }
    }

    const bool queueChanged = queue != previewTtsQueue;
    bool forceReload = queueChanged;
    if (!forceReload && rel == previewTtsRel && previewTtsLoaded)
    {
        if (plainExists && !previewTtsTemp.empty())
            forceReload = true;
        else if (plainExists && plainMod != 0 && plainMod != previewTtsModTime)
            forceReload = true;
        if (!forceReload)
            return;
    }

    unloadTts();
    previewTtsQueue = queue;
    previewTtsQueueIndex = 0;
    previewTtsRel = rel;
    previewTtsPath = rel;
    previewTtsModTime = plainMod;
    if (!loadMusicPath(rel, previewTts, previewTtsLoaded, previewTtsTemp, false))
    {
        previewTtsPath.clear();
        previewTtsRel.clear();
        previewTtsModTime = 0;
        previewTtsQueue.clear();
        previewTtsQueueIndex = 0;
    }
}

void DialogMediaPreview::toggleBed(
    Music& music,
    bool loaded,
    bool& playing,
    float volume)
{
    if (!loaded)
        return;
    if (previewTtsPlaying && previewTtsLoaded)
    {
        StopMusicStream(previewTts);
        previewTtsPlaying = false;
    }
    if (playing && IsMusicStreamPlaying(music))
    {
        PauseMusicStream(music);
        playing = false;
    }
    else
    {
        SetMusicVolume(music, volume);
        if (!IsMusicStreamPlaying(music))
            PlayMusicStream(music);
        else
            ResumeMusicStream(music);
        playing = true;
    }
}

void DialogMediaPreview::toggleTtsPreview()
{
    if (!previewTtsLoaded)
        return;
    if (previewTtsPlaying && IsMusicStreamPlaying(previewTts))
    {
        StopMusicStream(previewTts);
        previewTtsPlaying = false;
        return;
    }
    // Pause beds while dialog VO plays.
    if (previewMusicPlaying && previewMusicLoaded && IsMusicStreamPlaying(previewMusic))
    {
        PauseMusicStream(previewMusic);
        previewMusicPlaying = false;
    }
    if (previewAmbientPlaying && previewAmbientLoaded
        && IsMusicStreamPlaying(previewAmbient))
    {
        PauseMusicStream(previewAmbient);
        previewAmbientPlaying = false;
    }
    SeekMusicStream(previewTts, 0.0f);
    SetMusicVolume(previewTts, 1.0f);
    PlayMusicStream(previewTts);
    previewTtsPlaying = true;
}

void DialogMediaPreview::handleInput(Rectangle bounds, bool allowInteraction)
{
    updateStreams();
    if (!allowInteraction)
        return;

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, bounds))
        return;

    ensureSceneAudioLoaded();
    ensureSelectedTtsLoaded();

    const float contentY = bounds.y + 30.0f;
    const float contentH = bounds.height - 38.0f;
    const float imageW = std::clamp(bounds.width * 0.38f, 120.0f, 220.0f);
    const Rectangle controls = {
        bounds.x + 8.0f + imageW + kColGap,
        contentY,
        bounds.width - imageW - kColGap - 16.0f,
        contentH};

    float ty = controls.y + 28.0f;
    Rectangle musicBtn = {controls.x + 8.0f, ty, controls.width - 16.0f, 26.0f};
    ty += 64.0f;
    Rectangle ambientBtn = {controls.x + 8.0f, ty, controls.width - 16.0f, 26.0f};
    ty += 64.0f;
    Rectangle ttsBtn = {controls.x + 8.0f, ty, controls.width - 16.0f, 28.0f};

    if (!editorMousePressed(MOUSE_BUTTON_LEFT))
        return;

    if (previewMusicLoaded && CheckCollisionPointRec(mouse, musicBtn))
    {
        toggleBed(previewMusic, previewMusicLoaded, previewMusicPlaying, 0.90f);
        return;
    }
    if (previewAmbientLoaded && CheckCollisionPointRec(mouse, ambientBtn))
    {
        toggleBed(previewAmbient, previewAmbientLoaded, previewAmbientPlaying, 0.75f);
        return;
    }
    if (previewTtsLoaded && CheckCollisionPointRec(mouse, ttsBtn))
        toggleTtsPreview();
}

void DialogMediaPreview::draw(Rectangle bounds)
{
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    ensureSceneAudioLoaded();
    ensureSelectedTtsLoaded();
    updateStreams();

    DrawRectangleRec(bounds, kPanelFill);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelInnerEdge);

    DrawTextEx(
        font,
        "Dialog media",
        {bounds.x + 12.0f, bounds.y + 8.0f},
        kFontLabel,
        1.0f,
        kTextMuted);

    if (selectionSceneId == nullptr || selectionSceneId->empty()
        || docs == nullptr || !docs->scenes.hasScene(*selectionSceneId))
    {
        DrawTextEx(
            font,
            "Select a scene (tree)",
            {bounds.x + 12.0f, bounds.y + 36.0f},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    const float contentY = bounds.y + 30.0f;
    const float contentH = bounds.height - 38.0f;
    const float imageW = std::clamp(bounds.width * 0.38f, 120.0f, 220.0f);
    const Rectangle imageCol = {
        bounds.x + 8.0f, contentY, imageW, contentH};
    const Rectangle controls = {
        imageCol.x + imageCol.width + kColGap,
        contentY,
        bounds.width - imageW - kColGap - 16.0f,
        contentH};

    // ----- Left: scene image (compact) -----
    const float newStripH = 36.0f;
    const Rectangle imageBounds = {
        imageCol.x,
        imageCol.y,
        imageCol.width,
        std::max(48.0f, imageCol.height - newStripH - 6.0f)};
    const Rectangle newStrip = {
        imageCol.x,
        imageBounds.y + imageBounds.height + 6.0f,
        imageCol.width,
        newStripH};

    DrawRectangleRec(imageBounds, Color{18, 17, 22, 255});
    DrawRectangleLinesEx(imageBounds, 1.0f, kPanelInnerEdge);

    bool drewImage = false;
    if (thumbnails != nullptr && docs != nullptr)
    {
        ThumbnailEntry& entry = thumbnails->getOrLoad(
            *selectionSceneId,
            docs->scenes,
            docs->assetRoot,
            docs->resourceDir);
        if (entry.loaded && entry.texture.id != 0)
        {
            const float tw = static_cast<float>(entry.texture.width);
            const float th = static_cast<float>(entry.texture.height);
            const float pad = 6.0f;
            const float maxW = imageBounds.width - pad * 2.0f;
            const float maxH = imageBounds.height - pad * 2.0f;
            float dw = maxW;
            float dh = (th / std::max(1.0f, tw)) * dw;
            if (dh > maxH)
            {
                dh = maxH;
                dw = (tw / std::max(1.0f, th)) * dh;
            }
            const Rectangle dest = {
                imageBounds.x + (imageBounds.width - dw) * 0.5f,
                imageBounds.y + (imageBounds.height - dh) * 0.5f,
                dw,
                dh};
            DrawTexturePro(
                entry.texture,
                {0, 0, tw, th},
                dest,
                {0, 0},
                0.0f,
                WHITE);
            drewImage = true;
        }
    }
    if (!drewImage)
    {
        DrawTextEx(
            font,
            "No scene image",
            {imageBounds.x + 8.0f, imageBounds.y + 10.0f},
            kFontSmall,
            1.0f,
            kTextMuted);
    }
    DrawTextEx(
        font,
        "Scene",
        {imageBounds.x + 6.0f, imageBounds.y + imageBounds.height - 16.0f},
        kFontTiny,
        1.0f,
        Color{180, 160, 100, 200});

    DrawRectangleRec(newStrip, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(newStrip, 1.0f, kPanelInnerEdge);
    DrawTextEx(
        font,
        "New (empty)",
        {newStrip.x + 8.0f, newStrip.y + 10.0f},
        kFontTiny,
        1.0f,
        kTextDisabled);

    // ----- Right: beds + Play TTS -----
    DrawRectangleRec(controls, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(controls, 1.0f, kPanelInnerEdge);

    float ty = controls.y + 8.0f;
    DrawTextEx(font, "Music", {controls.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 16.0f;
    Rectangle musicBtn = {controls.x + 8.0f, ty, controls.width - 16.0f, 26.0f};
    const bool musicPlaying =
        previewMusicPlaying && previewMusicLoaded && IsMusicStreamPlaying(previewMusic);
    drawEditorButton(
        font,
        musicBtn,
        musicPlaying ? "Pause" : "Play",
        musicPlaying,
        previewMusicLoaded);
    ty += 32.0f;
    DrawTextEx(
        font,
        previewMusicPath.empty() ? "scene (none)" : "scene bed",
        {controls.x + 10.0f, ty},
        kFontTiny,
        1.0f,
        kTextMuted);

    ty += 20.0f;
    DrawTextEx(font, "Ambient", {controls.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 16.0f;
    Rectangle ambientBtn = {controls.x + 8.0f, ty, controls.width - 16.0f, 26.0f};
    const bool ambientPlaying =
        previewAmbientPlaying && previewAmbientLoaded
        && IsMusicStreamPlaying(previewAmbient);
    drawEditorButton(
        font,
        ambientBtn,
        ambientPlaying ? "Pause" : "Play",
        ambientPlaying,
        previewAmbientLoaded);
    ty += 32.0f;
    DrawTextEx(
        font,
        previewAmbientPath.empty() ? "scene (none)" : "scene bed",
        {controls.x + 10.0f, ty},
        kFontTiny,
        1.0f,
        kTextMuted);

    ty += 20.0f;
    DrawTextEx(
        font, "TTS dialog", {controls.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 16.0f;
    Rectangle ttsBtn = {controls.x + 8.0f, ty, controls.width - 16.0f, 28.0f};
    const bool ttsPlaying =
        previewTtsPlaying && previewTtsLoaded && IsMusicStreamPlaying(previewTts);
    drawEditorButton(
        font,
        ttsBtn,
        ttsPlaying ? "Stop TTS" : "Play TTS",
        ttsPlaying,
        previewTtsLoaded);
    ty += 34.0f;

    std::string ttsHint = "select Actor/Player node";
    if (flow != nullptr && flow->selectedNodeId > 0)
    {
        if (const DialogFlowNode* n = flow->findNode(flow->selectedNodeId))
        {
            if (n->kind == DialogNodeKind::ActorDialog
                || n->kind == DialogNodeKind::PlayerDialog)
            {
                ttsHint = previewTtsLoaded
                    ? (previewTtsRel.size() > 36
                           ? ("…" + previewTtsRel.substr(previewTtsRel.size() - 34))
                           : previewTtsRel)
                    : "no audio on disk";
            }
            else
                ttsHint = "not a dialog node";
        }
    }
    DrawTextEx(
        font,
        ttsHint.c_str(),
        {controls.x + 10.0f, ty},
        kFontTiny,
        1.0f,
        previewTtsLoaded ? kTextMuted : kTextDisabled);
}

} // namespace timberline_editor
