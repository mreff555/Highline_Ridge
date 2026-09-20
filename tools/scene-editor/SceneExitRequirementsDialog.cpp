/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneExitRequirementsDialog.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorPrefs.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "SceneAuthoring.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

using timberline_engine::builtinVoiceIds;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadAssetBytesFromFile;
using timberline_engine::normalizeVoiceId;
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

void backspaceUtf8(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

} // namespace

std::string SceneExitRequirementsDialog::defaultBlockedAudioPath() const
{
    return "resources/audio/tts/" + sceneId + "/blocked_" + direction + ".mp3";
}

std::string SceneExitRequirementsDialog::effectiveApiKey() const
{
    if (!sessionApiKey.empty())
        return sessionApiKey;
    if (docs == nullptr)
        return {};
    auto tryRead = [](const std::string& path) -> std::string {
        std::ifstream in(path.c_str());
        if (!in)
            return {};
        std::string key;
        std::getline(in, key);
        while (!key.empty()
               && (key.back() == '\r' || key.back() == '\n' || key.back() == ' '
                   || key.back() == '\t'))
            key.pop_back();
        return key;
    };
    const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::string key = tryRead(pathJoin(pathJoin(root, "resources"), "xai_api_key"));
    if (key.empty())
        key = tryRead(pathJoin(root, "xai_api_key"));
    if (key.empty() && !docs->resourceDir.empty())
        key = tryRead(pathJoin(docs->resourceDir, "xai_api_key"));
    return key;
}

void SceneExitRequirementsDialog::openForExit(
    const std::string& fromSceneId,
    const std::string& dir,
    const std::string& toId)
{
    sceneId = fromSceneId;
    direction = dir;
    toSceneId = toId;
    open = true;
    ignoreInputFrames = 2;
    waitMouseRelease = true;
    focusField = 2;
    scrollY = 0.0f;
    status.clear();
    error.clear();
    voiceMenuOpen = false;
    fieldContextOpen = false;
    stopPreviewVoice();
    loadFromScene();
}

void SceneExitRequirementsDialog::closeDialog()
{
    if (generateThread.joinable())
    {
        generateCancel.store(true);
        generateThread.join();
    }
    generateBusy = false;
    stopPreviewVoice();
    open = false;
    waitMouseRelease = false;
    voiceMenuOpen = false;
    fieldContextOpen = false;
}

void SceneExitRequirementsDialog::loadFromScene()
{
    requiresLightSource = false;
    requiresRoomPurchasedToday = false;
    requiresInventoryItem.clear();
    requiresStoryFlag.clear();
    blockBadge = "auto";
    blockedDetails.clear();
    blockedTtsText.clear();
    blockedTtsVoice = "leo";
    blockedTtsAudio = defaultBlockedAudioPath();
    if (graph == nullptr)
        return;
    const nlohmann::json req = graph->readExitRequirement(sceneId, direction);
    if (!req.is_object() || req.empty())
        return;
    requiresLightSource = req.value("requiresLightSource", false);
    requiresRoomPurchasedToday = req.value("requiresRoomPurchasedToday", false);
    requiresInventoryItem = req.value("requiresInventoryItem", "");
    requiresStoryFlag = req.value("requiresStoryFlag", "");
    blockBadge = req.value("blockBadge", req.value("badge", "auto"));
    if (blockBadge != "light" && blockBadge != "lock" && blockBadge != "gear")
        blockBadge = "auto";
    blockedDetails = req.value("blockedDetails", "");
    if (req.contains("blockedTts") && req["blockedTts"].is_object())
    {
        const auto& bag = req["blockedTts"];
        blockedTtsText = bag.value("ttsText", bag.value("text", ""));
        blockedTtsVoice = normalizeVoiceId(bag.value("ttsVoice", bag.value("voice", "leo")));
        blockedTtsAudio = bag.value("ttsAudio", defaultBlockedAudioPath());
    }
}

void SceneExitRequirementsDialog::suggestBadgeFromGates()
{
    if (requiresLightSource)
        blockBadge = "light";
    else if (!requiresInventoryItem.empty())
        blockBadge = "gear";
    else if (requiresRoomPurchasedToday || !requiresStoryFlag.empty())
        blockBadge = "lock";
    else
        blockBadge = "auto";
}

void SceneExitRequirementsDialog::cycleBadge()
{
    if (blockBadge == "auto")
        blockBadge = "light";
    else if (blockBadge == "light")
        blockBadge = "lock";
    else if (blockBadge == "lock")
        blockBadge = "gear";
    else
        blockBadge = "auto";
}

bool SceneExitRequirementsDialog::applyChanges()
{
    if (graph == nullptr || sceneId.empty() || direction.empty())
    {
        error = "Missing scene/direction.";
        return false;
    }
    nlohmann::json req = nlohmann::json::object();
    if (requiresLightSource)
        req["requiresLightSource"] = true;
    if (requiresRoomPurchasedToday)
        req["requiresRoomPurchasedToday"] = true;
    if (!requiresInventoryItem.empty())
        req["requiresInventoryItem"] = requiresInventoryItem;
    if (!requiresStoryFlag.empty())
        req["requiresStoryFlag"] = requiresStoryFlag;
    if (blockBadge != "auto")
        req["blockBadge"] = blockBadge;
    if (!blockedDetails.empty())
        req["blockedDetails"] = blockedDetails;
    if (!blockedTtsText.empty() || !blockedTtsAudio.empty())
    {
        nlohmann::json bag = nlohmann::json::object();
        bag["tts"] = !blockedTtsText.empty();
        bag["ttsText"] = blockedTtsText;
        bag["ttsVoice"] = blockedTtsVoice.empty() ? "leo" : blockedTtsVoice;
        bag["ttsAudio"] =
            blockedTtsAudio.empty() ? defaultBlockedAudioPath() : blockedTtsAudio;
        req["blockedTts"] = bag;
    }
    if (!graph->writeExitRequirement(sceneId, direction, req))
    {
        error = "Failed to write exitRequirements.";
        return false;
    }
    if (docs != nullptr)
        docs->scenes.save();
    if (docs != nullptr)
        docs->dirty = false;
    status = "Saved exit requirements for " + sceneId + "." + direction;
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

void SceneExitRequirementsDialog::clearRequirement()
{
    if (graph == nullptr)
        return;
    graph->clearExitRequirement(sceneId, direction);
    if (docs != nullptr)
    {
        docs->scenes.save();
        docs->dirty = false;
    }
    loadFromScene();
    status = "Cleared exit requirements.";
    error.clear();
    if (onSaved)
        onSaved();
}

std::string* SceneExitRequirementsDialog::focusedString()
{
    switch (focusField)
    {
    case 0:
        return &requiresInventoryItem;
    case 1:
        return &requiresStoryFlag;
    case 2:
        return &blockedDetails;
    case 3:
        return &blockedTtsText;
    case 4:
        return &sessionApiKey;
    default:
        return nullptr;
    }
}

void SceneExitRequirementsDialog::typeIntoFocusedField()
{
    std::string* buffer = focusedString();
    if (buffer == nullptr)
        return;
    const bool multiline = (focusField == 2 || focusField == 3);
    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp == '\n' || cp == '\r')
        {
            if (multiline)
                buffer->push_back('\n');
        }
        else if (cp >= 32)
            insertUtf8(*buffer, cp);
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspaceUtf8(*buffer);
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
            buffer->append(clip);
    }
}

bool SceneExitRequirementsDialog::blockedAudioExists() const
{
    if (docs == nullptr)
        return false;
    const std::string rel =
        blockedTtsAudio.empty() ? defaultBlockedAudioPath() : blockedTtsAudio;
    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, rel);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = rel;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(10);
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }
    for (const std::string& path : candidates)
    {
        if (FileExists(path.c_str()) || FileExists(compressedAssetPath(path).c_str()))
            return true;
    }
    return false;
}

void SceneExitRequirementsDialog::stopPreviewVoice()
{
    if (previewVoiceLoaded && IsMusicValid(previewVoice))
    {
        if (IsMusicStreamPlaying(previewVoice))
            StopMusicStream(previewVoice);
        UnloadMusicStream(previewVoice);
    }
    previewVoice = {};
    previewVoiceLoaded = false;
    previewVoicePlaying = false;
    if (!previewVoiceTempFile.empty())
    {
        std::remove(previewVoiceTempFile.c_str());
        previewVoiceTempFile.clear();
    }
}

void SceneExitRequirementsDialog::updatePreviewVoice()
{
    if (!previewVoiceLoaded || !previewVoicePlaying)
        return;
    if (!IsMusicValid(previewVoice))
    {
        stopPreviewVoice();
        return;
    }
    UpdateMusicStream(previewVoice);
    if (!IsMusicStreamPlaying(previewVoice))
        stopPreviewVoice();
}

void SceneExitRequirementsDialog::startPreviewVoice()
{
    if (docs == nullptr || generateBusy.load())
        return;
    if (!blockedAudioExists())
    {
        error = "No blocked TTS audio on disk yet — Generate Voice first.";
        return;
    }
    stopPreviewVoice();
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    const std::string rel =
        blockedTtsAudio.empty() ? defaultBlockedAudioPath() : blockedTtsAudio;
    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, rel);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = rel;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(10);
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }
    Music music{};
    std::string tempFile;
    bool ok = false;
    for (const std::string& path : candidates)
    {
        if (FileExists(path.c_str()))
        {
            music = LoadMusicStream(path.c_str());
            if (IsMusicValid(music))
            {
                ok = true;
                break;
            }
        }
        const std::string compressed = compressedAssetPath(path);
        if (!FileExists(compressed.c_str()))
            continue;
        std::vector<unsigned char> bytes;
        if (!loadAssetBytesFromFile(compressed, bytes) || bytes.empty())
            continue;
        const std::string tmp = pathJoin(
            GetApplicationDirectory() ? GetApplicationDirectory() : ".",
            "editor_blocked_tts_preview.mp3");
        std::ofstream out(tmp.c_str(), std::ios::binary);
        if (!out)
            continue;
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        out.close();
        music = LoadMusicStream(tmp.c_str());
        if (IsMusicValid(music))
        {
            tempFile = tmp;
            ok = true;
            break;
        }
        std::remove(tmp.c_str());
    }
    if (!ok)
    {
        error = "Could not load blocked TTS audio.";
        return;
    }
    music.looping = false;
    previewVoice = music;
    previewVoiceLoaded = true;
    previewVoicePlaying = true;
    previewVoiceTempFile = tempFile;
    SetMusicVolume(previewVoice, 1.0f);
    PlayMusicStream(previewVoice);
    status = "Previewing blocked TTS…";
    error.clear();
}

void SceneExitRequirementsDialog::startVoiceRefresh()
{
    if (docs == nullptr || generateBusy.load())
        return;
    if (!sceneTtsTextHasWord(blockedTtsText))
    {
        error = "Add blocked TTS text before generating voice.";
        return;
    }
    if (effectiveApiKey().empty())
    {
        error = "Paste an xAI API key (field below) before generating voice.";
        return;
    }
    if (!applyChanges())
        return;
    // Ensure scene has ttsEnabled so --refresh=<sceneId> collects bags.
    if (nlohmann::json* scene = docs->scenes.sceneJson(sceneId))
    {
        (*scene)["ttsEnabled"] = true;
        if (scene->value("ttsDefaultVoice", "").empty())
            (*scene)["ttsDefaultVoice"] =
                blockedTtsVoice.empty() ? "leo" : blockedTtsVoice;
        docs->scenes.save();
    }
    generateCancel.store(false);
    generateBusy = true;
    generateResultPending = false;
    const std::string keySnap = effectiveApiKey();
    const std::string idSnap = sceneId;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    if (generateThread.joinable())
        generateThread.join();
    generateThread = std::thread([this, keySnap, idSnap, assetRoot, resourceDir]() {
        const std::string msg = runSceneVoiceRefresh(
            assetRoot, resourceDir, idSnap, keySnap, &generateCancel);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
}

void SceneExitRequirementsDialog::pollGenerateResult()
{
    updatePreviewVoice();
    if (!generateResultPending)
        return;
    std::lock_guard<std::mutex> lock(generateMutex);
    if (!generateResultPending)
        return;
    status = generateResultStatus;
    generateResultPending = false;
    generateBusy = false;
    loadFromScene();
}

void SceneExitRequirementsDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
    pollGenerateResult();
    if (parchment != nullptr && parchment->blocksInput())
        return;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }
    if (waitMouseRelease)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeDialog();
        return;
    }
    typeIntoFocusedField();
}

void SceneExitRequirementsDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;
    pollGenerateResult();

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool busy = generateBusy.load();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0 && !busy
        && (parchment == nullptr || !parchment->blocksInput())
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);
    const float dialogW = std::min(720.0f, screenW - 32.0f);
    const float dialogH = std::min(640.0f, screenH - 32.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f, (screenH - dialogH) * 0.5f, dialogW, dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 16.0f;
    float y = dialog.y + pad;
    DrawTextEx(
        bold,
        ("Exit requirements: " + sceneId + " → " + direction).c_str(),
        {dialog.x + pad, y},
        kFontHeading,
        1.0f,
        kTextPrimary);
    y += 26.0f;
    DrawTextEx(
        font,
        ("Target: " + (toSceneId.empty() ? "(none)" : toSceneId)).c_str(),
        {dialog.x + pad, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 22.0f;

    const float fieldX = dialog.x + pad;
    const float fieldW = dialog.width - pad * 2.0f;
    const Rectangle content = {
        fieldX, y, fieldW, dialog.y + dialogH - 56.0f - y};

    auto toggle = [&](const char* label, bool& value, float ox) {
        const Rectangle btn = {fieldX + ox, y, 200.0f, 26.0f};
        drawEditorButton(
            font, btn, (std::string(label) + (value ? ": ON" : ": off")).c_str(), value, !busy);
        if (canClick && CheckCollisionPointRec(mouse, btn))
        {
            value = !value;
            if (blockBadge == "auto")
                suggestBadgeFromGates();
        }
    };
    toggle("Needs light", requiresLightSource, 0.0f);
    toggle("Room purchased", requiresRoomPurchasedToday, 210.0f);
    y += 34.0f;

    DrawTextEx(font, "Requires inventory item id", {fieldX, y}, kFontTiny, 1.0f, kTextMuted);
    y += 16.0f;
    Rectangle itemField = {fieldX, y, fieldW * 0.48f, 28.0f};
    DrawRectangleRec(itemField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        itemField, focusField == 0 ? 2.0f : 1.0f, focusField == 0 ? kPanelBorder : kPanelInnerEdge);
    DrawTextEx(
        font,
        requiresInventoryItem.empty() ? "(none)" : requiresInventoryItem.c_str(),
        {itemField.x + 8.0f, itemField.y + 6.0f},
        kFontSmall,
        1.0f,
        requiresInventoryItem.empty() ? kTextMuted : kTextPrimary);
    if (canClick && CheckCollisionPointRec(mouse, itemField))
        focusField = 0;

    DrawTextEx(
        font,
        "Requires story flag",
        {fieldX + fieldW * 0.52f, y - 16.0f},
        kFontTiny,
        1.0f,
        kTextMuted);
    Rectangle flagField = {fieldX + fieldW * 0.52f, y, fieldW * 0.48f, 28.0f};
    DrawRectangleRec(flagField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        flagField, focusField == 1 ? 2.0f : 1.0f, focusField == 1 ? kPanelBorder : kPanelInnerEdge);
    DrawTextEx(
        font,
        requiresStoryFlag.empty() ? "(none)" : requiresStoryFlag.c_str(),
        {flagField.x + 8.0f, flagField.y + 6.0f},
        kFontSmall,
        1.0f,
        requiresStoryFlag.empty() ? kTextMuted : kTextPrimary);
    if (canClick && CheckCollisionPointRec(mouse, flagField))
        focusField = 1;
    y += 40.0f;

    Rectangle badgeBtn = {fieldX, y, 220.0f, 28.0f};
    drawEditorButton(
        font,
        badgeBtn,
        ("Block badge: " + blockBadge).c_str(),
        true,
        !busy);
    if (canClick && CheckCollisionPointRec(mouse, badgeBtn))
        cycleBadge();
    Rectangle suggestBtn = {badgeBtn.x + 230.0f, y, 140.0f, 28.0f};
    drawEditorButton(font, suggestBtn, "Suggest badge", false, !busy);
    if (canClick && CheckCollisionPointRec(mouse, suggestBtn))
        suggestBadgeFromGates();
    y += 40.0f;

    DrawTextEx(font, "Blocked details (notebook)", {fieldX, y}, kFontTiny, 1.0f, kTextMuted);
    y += 16.0f;
    Rectangle detailsField = {fieldX, y, fieldW, 90.0f};
    DrawRectangleRec(detailsField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        detailsField,
        focusField == 2 ? 2.0f : 1.0f,
        focusField == 2 ? kPanelBorder : kPanelInnerEdge);
    BeginScissorMode(
        (int)detailsField.x + 2,
        (int)detailsField.y + 2,
        (int)detailsField.width - 4,
        (int)detailsField.height - 4);
    DrawTextEx(
        font,
        blockedDetails.empty() ? "(empty)" : blockedDetails.c_str(),
        {detailsField.x + 8.0f, detailsField.y + 6.0f},
        kFontSmall,
        1.0f,
        blockedDetails.empty() ? kTextMuted : kTextPrimary);
    EndScissorMode();
    if (canClick && CheckCollisionPointRec(mouse, detailsField))
        focusField = 2;
    if (parchment != nullptr && editorMousePressed(MOUSE_BUTTON_RIGHT)
        && CheckCollisionPointRec(mouse, detailsField))
    {
        fieldContextOpen = true;
        fieldContextTarget = 2;
        fieldContextRect = {mouse.x, mouse.y, 160.0f, 28.0f};
    }
    y += 100.0f;

    DrawTextEx(font, "Blocked TTS text", {fieldX, y}, kFontTiny, 1.0f, kTextMuted);
    y += 16.0f;
    Rectangle ttsField = {fieldX, y, fieldW, 90.0f};
    DrawRectangleRec(ttsField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        ttsField, focusField == 3 ? 2.0f : 1.0f, focusField == 3 ? kPanelBorder : kPanelInnerEdge);
    BeginScissorMode(
        (int)ttsField.x + 2, (int)ttsField.y + 2, (int)ttsField.width - 4, (int)ttsField.height - 4);
    DrawTextEx(
        font,
        blockedTtsText.empty() ? "(empty — paste or edit spoken markup)" : blockedTtsText.c_str(),
        {ttsField.x + 8.0f, ttsField.y + 6.0f},
        kFontSmall,
        1.0f,
        blockedTtsText.empty() ? kTextMuted : kTextPrimary);
    EndScissorMode();
    if (canClick && CheckCollisionPointRec(mouse, ttsField))
        focusField = 3;
    if (parchment != nullptr && editorMousePressed(MOUSE_BUTTON_RIGHT)
        && CheckCollisionPointRec(mouse, ttsField))
    {
        fieldContextOpen = true;
        fieldContextTarget = 3;
        fieldContextRect = {mouse.x, mouse.y, 160.0f, 28.0f};
    }
    y += 100.0f;

    voiceBtnRect = {fieldX, y, 160.0f, 28.0f};
    drawEditorButton(
        font,
        voiceBtnRect,
        ("Voice: " + (blockedTtsVoice.empty() ? "leo" : blockedTtsVoice)).c_str(),
        true,
        !busy);
    if (canClick && CheckCollisionPointRec(mouse, voiceBtnRect))
        voiceMenuOpen = !voiceMenuOpen;

    Rectangle genVoiceBtn = {fieldX + 170.0f, y, 150.0f, 28.0f};
    Rectangle previewBtn = {fieldX + 330.0f, y, 150.0f, 28.0f};
    const bool canGen = sceneTtsTextHasWord(blockedTtsText) && !effectiveApiKey().empty();
    const bool canPrev = blockedAudioExists();
    drawEditorButton(font, genVoiceBtn, busy ? "Working…" : "Generate Voice", true, !busy && canGen);
    drawEditorButton(
        font,
        previewBtn,
        previewVoicePlaying ? "Stop preview" : "Preview voice",
        true,
        !busy && canPrev);
    if (canClick && canGen && CheckCollisionPointRec(mouse, genVoiceBtn))
        startVoiceRefresh();
    if (canClick && canPrev && CheckCollisionPointRec(mouse, previewBtn))
    {
        if (previewVoicePlaying)
            stopPreviewVoice();
        else
            startPreviewVoice();
    }
    y += 40.0f;

    DrawTextEx(font, "xAI API key (session)", {fieldX, y}, kFontTiny, 1.0f, kTextMuted);
    y += 16.0f;
    Rectangle keyField = {fieldX, y, fieldW, 28.0f};
    DrawRectangleRec(keyField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        keyField, focusField == 4 ? 2.0f : 1.0f, focusField == 4 ? kPanelBorder : kPanelInnerEdge);
    const std::string keyShown =
        sessionApiKey.empty() ? "(uses editor prefs if set)" : std::string(sessionApiKey.size(), '*');
    DrawTextEx(
        font,
        keyShown.c_str(),
        {keyField.x + 8.0f, keyField.y + 6.0f},
        kFontSmall,
        1.0f,
        sessionApiKey.empty() ? kTextMuted : kTextPrimary);
    if (canClick && CheckCollisionPointRec(mouse, keyField))
        focusField = 4;

    // Footer
    const float btnY = dialog.y + dialogH - 48.0f;
    Rectangle saveBtn = {dialog.x + pad, btnY, 110.0f, 32.0f};
    Rectangle clearBtn = {saveBtn.x + 120.0f, btnY, 110.0f, 32.0f};
    Rectangle cancelBtn = {clearBtn.x + 120.0f, btnY, 110.0f, 32.0f};
    drawEditorButton(font, saveBtn, "Save", true, !busy);
    drawEditorButton(font, clearBtn, "Clear", false, !busy);
    drawEditorButton(font, cancelBtn, "Close", false, true);
    if (canClick && CheckCollisionPointRec(mouse, saveBtn))
        applyChanges();
    if (canClick && CheckCollisionPointRec(mouse, clearBtn))
        clearRequirement();
    if (canClick && CheckCollisionPointRec(mouse, cancelBtn))
        closeDialog();

    if (!status.empty())
        DrawTextEx(
            font,
            status.c_str(),
            {cancelBtn.x + 120.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    if (!error.empty())
        DrawTextEx(
            font,
            error.c_str(),
            {cancelBtn.x + 120.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{220, 90, 80, 255});

    if (voiceMenuOpen)
    {
        const auto& voices = builtinVoiceIds();
        const float rowH = 22.0f;
        voiceMenuRect = {
            voiceBtnRect.x,
            voiceBtnRect.y + voiceBtnRect.height + 2.0f,
            160.0f,
            rowH * static_cast<float>(voices.size()) + 4.0f};
        DrawRectangleRec(voiceMenuRect, Color{36, 32, 44, 255});
        DrawRectangleLinesEx(voiceMenuRect, 1.0f, kPanelBorder);
        float my = voiceMenuRect.y + 2.0f;
        for (const std::string& v : voices)
        {
            Rectangle row = {voiceMenuRect.x + 2.0f, my, voiceMenuRect.width - 4.0f, rowH - 2.0f};
            if (CheckCollisionPointRec(mouse, row))
            {
                DrawRectangleRec(row, Color{60, 54, 72, 220});
                if (canClick)
                {
                    blockedTtsVoice = normalizeVoiceId(v);
                    voiceMenuOpen = false;
                }
            }
            DrawTextEx(font, v.c_str(), {row.x + 6.0f, row.y + 2.0f}, kFontSmall, 1.0f, kTextPrimary);
            my += rowH;
        }
        if (canClick && !CheckCollisionPointRec(mouse, voiceMenuRect)
            && !CheckCollisionPointRec(mouse, voiceBtnRect))
            voiceMenuOpen = false;
    }

    if (fieldContextOpen)
    {
        DrawRectangleRec(fieldContextRect, Color{32, 28, 40, 245});
        DrawRectangleLinesEx(fieldContextRect, 1.0f, kPanelBorder);
        DrawTextEx(
            font,
            "Edit full screen",
            {fieldContextRect.x + 10.0f, fieldContextRect.y + 6.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        if (canClick)
        {
            if (CheckCollisionPointRec(mouse, fieldContextRect) && parchment != nullptr
                && docs != nullptr)
            {
                std::string* target =
                    fieldContextTarget == 3 ? &blockedTtsText : &blockedDetails;
                parchment->openEditor(
                    target,
                    fieldContextTarget == 3,
                    fieldContextTarget == 3 ? "Blocked TTS" : "Blocked details",
                    docs->resourceDir,
                    docs->assetRoot);
                parchment->onClosed = [this]() { waitMouseRelease = true; };
                waitMouseRelease = true;
            }
            fieldContextOpen = false;
        }
    }

    if (canClick && !CheckCollisionPointRec(mouse, dialog) && !fieldContextOpen && !voiceMenuOpen)
        closeDialog();

    (void)content;
}

} // namespace timberline_editor
