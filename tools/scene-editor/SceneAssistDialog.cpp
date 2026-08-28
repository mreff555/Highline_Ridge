/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneAssistDialog.h"
#include "EditorButton.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadAssetBytesFromFile;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

void insertUtf8(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    char bytes[5] = {};
    int size = 0;
    if (codepoint < 0x80)
    {
        bytes[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    buffer.append(bytes, bytes + size);
}

void backspace(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

void drawFieldBox(Rectangle r, bool focused)
{
    DrawRectangleRec(r, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(r, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
}

void drawLabel(Font font, const char* text, float x, float y)
{
    DrawTextEx(font, text, {x, y}, kFontTiny, 1.0f, kTextMuted);
}

} // namespace

void SceneAssistDialog::ensureAudio()
{
    if (audioDeviceReady)
        return;
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    audioDeviceReady = IsAudioDeviceReady();
}

void SceneAssistDialog::stopPreviewMusic()
{
    if (previewMusicLoaded && IsMusicStreamPlaying(previewMusic))
        StopMusicStream(previewMusic);
    previewMusicPlaying = false;
}

void SceneAssistDialog::unloadPreviewAssets()
{
    stopPreviewMusic();
    if (previewMusicLoaded)
    {
        UnloadMusicStream(previewMusic);
        previewMusic = {};
        previewMusicLoaded = false;
    }
    if (!previewMusicTempFile.empty())
    {
        std::remove(previewMusicTempFile.c_str());
        previewMusicTempFile.clear();
    }
    if (previewTextureLoaded && previewTexture.id != 0)
    {
        UnloadTexture(previewTexture);
        previewTexture = {};
        previewTextureLoaded = false;
    }
}

void SceneAssistDialog::openForScene(const std::string& sceneId)
{
    if (docs == nullptr || sceneId.empty() || !docs->scenes.hasScene(sceneId))
        return;

    unloadPreviewAssets();
    previewPending = false;
    previewTarget = 0;
    previewRelPath.clear();
    liveRelPath.clear();
    generateBusy = false;
    generateResultPending = false;
    generateTarget = 0;
    error.clear();
    status.clear();
    focusField = 0;

    if (!fillPayloadFromScene(*docs, sceneId, payload))
    {
        error = "Failed to load scene fields.";
        return;
    }

    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
    descScrollY = 0.0f;
}

void SceneAssistDialog::closeDialog()
{
    if (previewPending)
        revertPreview();
    if (generateThread.joinable())
        generateThread.join();
    generateBusy = false;
    unloadPreviewAssets();
    open = false;
    error.clear();
    status.clear();
}

bool SceneAssistDialog::hasPreviewOverride(const std::string& sceneId) const
{
    return previewPending && payload.id == sceneId && !previewRelPath.empty();
}

std::string SceneAssistDialog::overrideImagePath(const std::string& sceneId) const
{
    if (hasPreviewOverride(sceneId) && previewTarget == 1)
        return previewRelPath;
    return "";
}

std::string SceneAssistDialog::overrideAmbientPath(const std::string& sceneId) const
{
    if (hasPreviewOverride(sceneId) && previewTarget == 2)
        return previewRelPath;
    return "";
}

std::string SceneAssistDialog::overrideMusicPath(const std::string& sceneId) const
{
    if (hasPreviewOverride(sceneId) && previewTarget == 3)
        return previewRelPath;
    return "";
}

Texture2D* SceneAssistDialog::overrideImageTexture(const std::string& sceneId)
{
    if (hasPreviewOverride(sceneId) && previewTarget == 1 && previewTextureLoaded)
        return &previewTexture;
    return nullptr;
}

bool SceneAssistDialog::loadPreviewTexture(const std::string& relPath)
{
    if (previewTextureLoaded && previewTexture.id != 0)
    {
        UnloadTexture(previewTexture);
        previewTexture = {};
        previewTextureLoaded = false;
    }
    if (docs == nullptr || relPath.empty())
        return false;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relPath);
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str())
            && loadTextureFromAssetFile(compressed, previewTexture))
        {
            previewTextureLoaded = true;
            return true;
        }
        if (FileExists(path.c_str())
            && loadTextureFromAssetFile(path, previewTexture))
        {
            previewTextureLoaded = true;
            return true;
        }
    }
    return false;
}

bool SceneAssistDialog::loadPreviewMusic(const std::string& relPath)
{
    stopPreviewMusic();
    if (previewMusicLoaded)
    {
        UnloadMusicStream(previewMusic);
        previewMusic = {};
        previewMusicLoaded = false;
    }
    if (!previewMusicTempFile.empty())
    {
        std::remove(previewMusicTempFile.c_str());
        previewMusicTempFile.clear();
    }
    ensureAudio();
    if (!audioDeviceReady || docs == nullptr || relPath.empty())
        return false;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, relPath);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }

    for (const std::string& path : candidates)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(path.c_str()))
        {
            previewMusic = LoadMusicStream(path.c_str());
            if (IsMusicValid(previewMusic))
            {
                previewMusic.looping = true;
                previewMusicLoaded = true;
                return true;
            }
        }
        if (FileExists(compressed.c_str()))
        {
            std::vector<unsigned char> bytes;
            if (loadAssetBytesFromFile(compressed, bytes) && !bytes.empty())
            {
                std::string fileType = ".mp3";
                const size_t dot = path.find_last_of('.');
                if (dot != std::string::npos)
                    fileType = path.substr(dot);
                const std::string tmp = pathJoin(
                    GetApplicationDirectory() ? GetApplicationDirectory() : ".",
                    "editor_scene_preview_bed.tmp" + fileType);
                std::ofstream out(tmp.c_str(), std::ios::binary);
                if (out)
                {
                    out.write(
                        reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                    out.close();
                    previewMusic = LoadMusicStream(tmp.c_str());
                    if (IsMusicValid(previewMusic))
                    {
                        previewMusic.looping = true;
                        previewMusicLoaded = true;
                        previewMusicTempFile = tmp;
                        return true;
                    }
                    std::remove(tmp.c_str());
                }
            }
        }
    }
    return false;
}

void SceneAssistDialog::pollGenerateResult()
{
    if (!generateResultPending)
        return;
    std::lock_guard<std::mutex> lock(generateMutex);
    if (!generateResultPending)
        return;

    const std::string resultMsg = generateResultStatus;
    generateResultPending = false;
    generateBusy = false;

    const bool ok = resultMsg.find("finished OK") != std::string::npos;
    if (!ok)
    {
        error = resultMsg;
        status.clear();
        generateTarget = 0;
        return;
    }
    status = resultMsg;

    previewPending = true;
    previewTarget = generateTarget;
    previewRelPath = sceneAiPreviewRelPath(payload.id, previewTarget);
    liveRelPath = sceneAiLiveRelPath(payload, previewTarget);
    generateTarget = 0;

    unloadPreviewAssets();
    if (previewTarget == 1)
    {
        if (!loadPreviewTexture(previewRelPath))
            error = "Generated, but could not load preview image.";
        else
            status = "Preview ready — Accept to keep, or Revert.";
    }
    else if (previewTarget == 2 || previewTarget == 3)
    {
        if (!loadPreviewMusic(previewRelPath))
            error = "Generated, but could not load preview audio.";
        else
        {
            status = "Preview ready — Play below, then Accept or Revert.";
            PlayMusicStream(previewMusic);
            previewMusicPlaying = true;
        }
    }

    if (onAccepted)
    {
        // Notify canvas to refresh transport paths for override playback.
        // Not "accepted" yet — callback name is used as a general refresh hook.
    }
}

void SceneAssistDialog::startGenerate(int target)
{
    if (docs == nullptr || generateBusy.load() || previewPending)
        return;
    if (target < 1 || target > 3)
        return;

    error.clear();
    if (payload.description.empty())
    {
        error = "Scene description is empty — AI needs it as context. "
                "Edit the description first.";
        return;
    }
    if (target == 1 && sessionApiKey.empty())
    {
        // Images require a key. Check common on-disk / env sources before failing.
        bool haveFallback = false;
        if (const char* env = std::getenv("XAI_API_KEY");
            env != nullptr && env[0] != '\0')
            haveFallback = true;
        if (!haveFallback && docs != nullptr)
        {
            using timberline_engine::pathJoin;
            const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
            const std::string a = pathJoin(pathJoin(root, "resources"), "xai_api_key");
            const std::string b = pathJoin(root, "xai_api_key");
            if (FileExists(a.c_str()) || FileExists(b.c_str()))
                haveFallback = true;
        }
        if (!haveFallback)
        {
            error =
                "Paste an xAI API key into the key field (Cmd/Ctrl+V), then try again.";
            status.clear();
            return;
        }
        status = "No session key — using XAI_API_KEY / resources/xai_api_key.";
    }

    const std::string jobsPath = writeSceneAiPreviewJobsFile(
        docs->assetRoot, docs->resourceDir, payload, target);
    if (jobsPath.empty())
    {
        error = "Failed to write AI preview jobs file.";
        return;
    }

    generateBusy = true;
    generateTarget = target;
    generateResultPending = false;
    status = "Generating…";
    const std::string keySnap = sessionApiKey;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    if (generateThread.joinable())
        generateThread.join();
    generateThread = std::thread([this, keySnap, jobsPath, assetRoot, resourceDir]() {
        const std::string msg =
            runSceneAuthoringAiJobsFile(assetRoot, resourceDir, jobsPath, keySnap);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
}

void SceneAssistDialog::acceptPreview()
{
    if (!previewPending || docs == nullptr)
        return;

    stopPreviewMusic();
    std::string err;
    const int target = previewTarget;
    const std::string sceneId = payload.id;
    if (!acceptSceneAiPreview(
            *docs, sceneId, target, previewRelPath, liveRelPath, err))
    {
        error = err.empty() ? "Accept failed." : err;
        return;
    }

    if (thumbnails != nullptr && target == 1)
        thumbnails->invalidate(sceneId);

    // Refresh payload paths from scene.
    fillPayloadFromScene(*docs, sceneId, payload);
    unloadPreviewAssets();
    previewPending = false;
    previewTarget = 0;
    previewRelPath.clear();
    liveRelPath.clear();
    status = "Accepted — scene assets updated.";
    error.clear();

    if (onAccepted)
        onAccepted();
}

void SceneAssistDialog::revertPreview()
{
    if (!previewPending)
        return;

    stopPreviewMusic();
    if (docs != nullptr)
    {
        discardSceneAiPreviewFiles(
            docs->assetRoot, docs->resourceDir, previewRelPath);
    }
    unloadPreviewAssets();
    previewPending = false;
    previewTarget = 0;
    previewRelPath.clear();
    liveRelPath.clear();
    status = "Reverted — kept previous assets.";
    error.clear();

    if (onAccepted)
        onAccepted();
}

void SceneAssistDialog::typeIntoFocusedField()
{
    if (focusField != 0)
        return;
    std::string* target = &sessionApiKey;

    // Cmd/Ctrl+V paste (same pattern as item editor AI Assist key field).
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            std::string pasted = clip;
            while (!pasted.empty()
                   && (pasted.back() == '\n' || pasted.back() == '\r'
                       || pasted.back() == ' ' || pasted.back() == '\t'))
                pasted.pop_back();
            size_t start = 0;
            while (start < pasted.size()
                   && (pasted[start] == ' ' || pasted[start] == '\t'
                       || pasted[start] == '\n' || pasted[start] == '\r'))
                ++start;
            if (start > 0)
                pasted = pasted.substr(start);
            // Strip interior newlines (keys are single-line).
            pasted.erase(
                std::remove_if(
                    pasted.begin(),
                    pasted.end(),
                    [](unsigned char ch) { return ch == '\n' || ch == '\r'; }),
                pasted.end());
            *target = pasted;
            status = "API key pasted (session only).";
            error.clear();
        }
        // Drain any leftover char events from the paste chord.
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp >= 32 && cp != '\n')
            insertUtf8(*target, cp);
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspace(*target);
}

void SceneAssistDialog::updateAudio()
{
    if (previewMusicLoaded && previewMusicPlaying)
        UpdateMusicStream(previewMusic);
}

void SceneAssistDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
    pollGenerateResult();
    updateAudio();

    if (waitMouseRelease)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }

    if (!generateBusy.load())
        typeIntoFocusedField();

    if (IsKeyPressed(KEY_ESCAPE) && !generateBusy.load())
    {
        if (previewPending)
            revertPreview();
        else
            closeDialog();
    }
}

void SceneAssistDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;
    pollGenerateResult();
    updateAudio();

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool busy = generateBusy.load();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0 && !busy
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(780.0f, screenW - 40.0f);
    const float dialogH = std::min(640.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Scene AI Assist",
        {dialog.x + 20.0f, dialog.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);

    const std::string subtitle =
        payload.id.empty() ? "" : ("Scene: " + payload.id);
    DrawTextEx(
        font,
        subtitle.c_str(),
        {dialog.x + 20.0f, dialog.y + 46.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Footer holds regenerate / Accept-Revert + Close. Keep content above it.
    const float pad = 16.0f;
    const float actionBtnH = 34.0f;
    const float closeBtnH = 36.0f;
    const float statusStripH = 36.0f;
    const float footerH = actionBtnH + 10.0f + closeBtnH + 12.0f + statusStripH;
    const float contentTop = dialog.y + 68.0f;
    const Rectangle content = {
        dialog.x + pad,
        contentTop,
        dialog.width - pad * 2.0f,
        dialogH - (contentTop - dialog.y) - footerH};
    DrawRectangleRec(content, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(content, 1.0f, kPanelInnerEdge);

    const float innerPad = 14.0f;
    const float labelX = content.x + innerPad;
    const float fieldX = content.x + innerPad;
    const float fieldW = content.width - innerPad * 2.0f;

    // Fixed blocks: API key (+ optional preview chrome). Description fills the rest.
    const float keyBlockH = 16.0f + 32.0f; // label + field
    const float previewReserve =
        previewPending ? (previewTarget == 1 ? 180.0f : 70.0f) : 0.0f;
    const float gapAfterDesc = 14.0f;
    const float gapAfterKey = previewPending ? 14.0f : 0.0f;

    float y = content.y + innerPad;
    drawLabel(font, "Description (AI context — edit via Scene Variables)", labelX, y);
    y += 18.0f;

    const float descBoxH = std::max(
        96.0f,
        content.height - innerPad * 2.0f - 18.0f - keyBlockH - gapAfterDesc
            - previewReserve - gapAfterKey);
    const Rectangle descBox = {fieldX, y, fieldW, descBoxH};
    drawFieldBox(descBox, false);

    const std::string& descText =
        payload.description.empty() ? std::string("(empty)") : payload.description;
    const float descFont = kFontSmall;
    const float descLineH = descFont + 4.0f;
    const float descPad = 8.0f;
    const std::vector<EditorVisualLine> descLines = layoutWrappedTextLines(
        font, descText, descBox.width - descPad * 2.0f, descFont);
    const float descContentH =
        std::max(descLineH, static_cast<float>(descLines.size()) * descLineH);
    const float descMaxScroll =
        std::max(0.0f, descContentH + descPad * 2.0f - descBox.height);
    if (descScrollY > descMaxScroll)
        descScrollY = descMaxScroll;
    if (descScrollY < 0.0f)
        descScrollY = 0.0f;

    drawVisualTextLines(
        font,
        descLines,
        descBox,
        descPad,
        descFont,
        descLineH,
        descScrollY,
        payload.description.empty() ? kTextMuted : kTextPrimary);

    if (descMaxScroll > 0.0f && CheckCollisionPointRec(mouse, descBox))
    {
        descScrollY -= GetMouseWheelMove() * descLineH * 2.0f;
        if (descScrollY < 0.0f)
            descScrollY = 0.0f;
        if (descScrollY > descMaxScroll)
            descScrollY = descMaxScroll;
    }
    // Subtle scrollbar when content overflows.
    if (descMaxScroll > 0.0f)
    {
        const float trackX = descBox.x + descBox.width - 5.0f;
        const float trackH = descBox.height - 4.0f;
        const float thumbH = std::max(18.0f, trackH * (descBox.height / (descContentH + descPad * 2.0f)));
        const float thumbY =
            descBox.y + 2.0f + (trackH - thumbH) * (descScrollY / descMaxScroll);
        DrawRectangle(
            static_cast<int>(trackX),
            static_cast<int>(descBox.y + 2.0f),
            3,
            static_cast<int>(trackH),
            Color{40, 36, 48, 255});
        DrawRectangle(
            static_cast<int>(trackX),
            static_cast<int>(thumbY),
            3,
            static_cast<int>(thumbH),
            kPanelBorder);
    }

    y = descBox.y + descBox.height + gapAfterDesc;

    drawLabel(
        font,
        "Session xAI API key (Cmd/Ctrl+V to paste; not saved to disk)",
        labelX,
        y);
    y += 16.0f;
    const Rectangle keyField = {fieldX, y, fieldW, 32.0f};
    drawFieldBox(keyField, focusField == 0 && !previewPending);
    const std::string keyShow = sessionApiKey.empty()
        ? "(optional if XAI_API_KEY is set)"
        : std::string(std::min<size_t>(sessionApiKey.size(), 28), '*');
    DrawTextEx(
        font,
        keyShow.c_str(),
        {keyField.x + 8.0f, keyField.y + 8.0f},
        kFontSmall,
        1.0f,
        sessionApiKey.empty() ? kTextMuted : kTextPrimary);
    if (canClick && CheckCollisionPointRec(mouse, keyField) && !previewPending)
        focusField = 0;
    y = keyField.y + keyField.height + gapAfterKey;

    if (previewPending)
    {
        const char* kind =
            previewTarget == 1 ? "Image preview"
            : previewTarget == 2 ? "Ambient preview"
                                 : "Music preview";
        DrawTextEx(font, kind, {labelX, y}, kFontSmall, 1.0f, kTextPrimary);
        y += 20.0f;

        if (previewTarget == 1 && previewTextureLoaded)
        {
            const float maxW = fieldW;
            const float maxH = content.y + content.height - y - innerPad;
            const float tw = static_cast<float>(previewTexture.width);
            const float th = static_cast<float>(previewTexture.height);
            float dw = maxW;
            float dh = (th / std::max(1.0f, tw)) * dw;
            if (dh > maxH && maxH > 40.0f)
            {
                dh = maxH;
                dw = (tw / std::max(1.0f, th)) * dh;
            }
            const Rectangle dest = {
                fieldX + (fieldW - dw) * 0.5f, y, dw, std::max(40.0f, dh)};
            DrawTexturePro(
                previewTexture,
                {0, 0, tw, th},
                dest,
                {0, 0},
                0.0f,
                WHITE);
            DrawRectangleLinesEx(dest, 1.0f, kPanelBorder);
        }
        else if (previewTarget == 2 || previewTarget == 3)
        {
            const Rectangle playBtn = {fieldX, y, 120.0f, 32.0f};
            const bool playing = previewMusicPlaying
                && previewMusicLoaded
                && IsMusicStreamPlaying(previewMusic);
            drawEditorButton(
                font,
                playBtn,
                playing ? "Pause" : "Play",
                true,
                previewMusicLoaded);
            if (canClick && previewMusicLoaded
                && CheckCollisionPointRec(mouse, playBtn))
            {
                if (playing)
                {
                    PauseMusicStream(previewMusic);
                    previewMusicPlaying = false;
                }
                else
                {
                    if (!IsMusicStreamPlaying(previewMusic))
                        PlayMusicStream(previewMusic);
                    else
                        ResumeMusicStream(previewMusic);
                    previewMusicPlaying = true;
                }
            }
            DrawTextEx(
                font,
                previewRelPath.c_str(),
                {playBtn.x + playBtn.width + 12.0f, playBtn.y + 8.0f},
                kFontTiny,
                1.0f,
                kTextMuted);
        }
    }

    // Footer actions — regenerate buttons sit above Close.
    const float btnY = dialog.y + dialogH - footerH + 8.0f;
    const float closeBtnW = 120.0f;
    const Rectangle closeBtn = {
        dialog.x + dialogW - closeBtnW - pad,
        dialog.y + dialogH - closeBtnH - 10.0f - statusStripH,
        closeBtnW,
        closeBtnH};

    if (previewPending)
    {
        const Rectangle acceptBtn = {dialog.x + pad, btnY, 130.0f, actionBtnH};
        const Rectangle revertBtn = {acceptBtn.x + 140.0f, btnY, 130.0f, actionBtnH};
        drawEditorButton(font, acceptBtn, "Accept", true, !busy);
        drawEditorButton(font, revertBtn, "Revert", false, !busy);
        if (canClick)
        {
            if (CheckCollisionPointRec(mouse, acceptBtn))
                acceptPreview();
            else if (CheckCollisionPointRec(mouse, revertBtn))
                revertPreview();
        }
    }
    else
    {
        const bool actionsEnabled = !busy;
        const float gap = 10.0f;
        const float regenW = (dialogW - pad * 2.0f - gap * 2.0f) / 3.0f;
        struct GenRow
        {
            const char* label;
            int target;
        };
        const GenRow rows[] = {
            {"Regenerate image", 1},
            {"Regenerate ambient", 2},
            {"Regenerate music", 3},
        };
        for (int i = 0; i < 3; ++i)
        {
            const Rectangle btn = {
                dialog.x + pad + static_cast<float>(i) * (regenW + gap),
                btnY,
                regenW,
                actionBtnH};
            drawEditorButton(font, btn, rows[i].label, true, actionsEnabled);
            if (canClick && actionsEnabled && CheckCollisionPointRec(mouse, btn))
                startGenerate(rows[i].target);
            if (busy && generateTarget == rows[i].target)
            {
                DrawTextEx(
                    font,
                    "…",
                    {btn.x + btn.width * 0.5f - 4.0f, btn.y + 8.0f},
                    kFontTiny,
                    1.0f,
                    Color{220, 80, 70, 255});
            }
        }
    }

    drawEditorButton(font, closeBtn, "Close", false, !busy);
    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, closeBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog) && !busy && !previewPending)
            closeDialog();
    }

    const float statusY = dialog.y + dialogH - statusStripH + 4.0f;
    if (!status.empty())
    {
        DrawTextEx(
            font,
            status.c_str(),
            {dialog.x + pad, statusY},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    }
    if (!error.empty())
    {
        DrawTextEx(
            font,
            error.c_str(),
            {dialog.x + pad, statusY + (status.empty() ? 0.0f : 14.0f)},
            kFontTiny,
            1.0f,
            Color{220, 100, 90, 255});
    }
    if (busy)
    {
        const float pulse =
            0.45f + 0.55f * (0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 5.0f));
        DrawTextEx(
            font,
            "Working…",
            {dialog.x + dialogW * 0.5f - 40.0f, dialog.y + 16.0f},
            kFontSmall,
            1.0f,
            Color{
                220,
                static_cast<unsigned char>(40 + 40 * (1.0f - pulse)),
                static_cast<unsigned char>(40 + 40 * (1.0f - pulse)),
                255});
    }
}

} // namespace timberline_editor
