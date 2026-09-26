/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneExitRequirementsDialog.h"
#include "EditorPaths.h"
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
#include <cstdlib>
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

std::string ellipsizeUi(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth)
{
    if (text.empty()
        || MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    std::string out = text;
    const std::string ellipsis = "...";
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

void drawClippedFieldText(
    Font font,
    const Rectangle& field,
    const std::string& text,
    const char* placeholder,
    float fontSize,
    Color color)
{
    BeginScissorMode(
        (int)field.x + 2, (int)field.y + 2, (int)field.width - 4, (int)field.height - 4);
    const std::string shown = text.empty()
        ? std::string(placeholder ? placeholder : "")
        : ellipsizeUi(font, text, fontSize, field.width - 16.0f);
    DrawTextEx(
        font,
        shown.c_str(),
        {field.x + 8.0f, field.y + 6.0f},
        fontSize,
        1.0f,
        color);
    EndScissorMode();
}

std::string findReturnDirection(
    SceneGraphModel* graph,
    const std::string& fromId,
    const std::string& toId,
    const std::string& forwardDir)
{
    if (graph == nullptr || fromId.empty() || toId.empty())
        return {};
    const std::string opp = graph->oppositeDirection(forwardDir);
    if (!opp.empty() && graph->getExitTarget(toId, opp) == fromId)
        return opp;
    static const char* kDirs[] = {
        "forward", "backward", "left", "right", "up", "down"};
    for (const char* d : kDirs)
    {
        if (graph->getExitTarget(toId, d) == fromId)
            return d;
    }
    return {};
}

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

std::string SceneExitRequirementsDialog::defaultVariantAudioPath(int index) const
{
    return "resources/audio/tts/" + sceneId + "/blocked_" + direction + "_v"
        + std::to_string(index) + ".mp3";
}

void SceneExitRequirementsDialog::addBlockedVariant()
{
    ExitBlockedVariantEdit v;
    v.ttsVoice = blockedTtsVoice.empty() ? "leo" : blockedTtsVoice;
    v.ttsAudio = defaultVariantAudioPath(static_cast<int>(blockedVariants.size()));
    blockedVariants.push_back(v);
    selectedVariant = static_cast<int>(blockedVariants.size()) - 1;
    focusField = 5;
    status = "Added blocked variant — set when / details (empty when = default).";
    error.clear();
}

void SceneExitRequirementsDialog::removeSelectedVariant()
{
    if (selectedVariant < 0 || selectedVariant >= static_cast<int>(blockedVariants.size()))
        return;
    blockedVariants.erase(blockedVariants.begin() + selectedVariant);
    if (blockedVariants.empty())
        selectedVariant = -1;
    else if (selectedVariant >= static_cast<int>(blockedVariants.size()))
        selectedVariant = static_cast<int>(blockedVariants.size()) - 1;
    status = "Removed blocked variant.";
    error.clear();
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
    if (const char* env = std::getenv("XAI_API_KEY");
        env != nullptr && env[0] != '\0')
        return std::string(env);
    const std::string keyPath = resolveXaiApiKeyFile(docs->resourceDir);
    if (!keyPath.empty())
        return tryRead(keyPath);
    return {};
}

void SceneExitRequirementsDialog::resolveWireSides(
    const std::string& fromSceneId,
    const std::string& dir,
    const std::string& toId)
{
    sideFrom[0] = fromSceneId;
    sideDir[0] = dir;
    sideTo[0] = toId;
    sideFrom[1].clear();
    sideDir[1].clear();
    sideTo[1].clear();
    reverseAvailable = false;
    editingReverse = false;

    const std::string returnDir =
        findReturnDirection(graph, fromSceneId, toId, dir);
    if (!returnDir.empty() && !toId.empty())
    {
        sideFrom[1] = toId;
        sideDir[1] = returnDir;
        sideTo[1] = fromSceneId;
        reverseAvailable = true;
    }
}

void SceneExitRequirementsDialog::applyActiveSide()
{
    const int side = editingReverse ? 1 : 0;
    sceneId = sideFrom[side];
    direction = sideDir[side];
    toSceneId = sideTo[side];
}

bool SceneExitRequirementsDialog::setEditingReverse(bool reverse)
{
    if (reverse && !reverseAvailable)
        return false;
    if (reverse == editingReverse)
        return true;
    // Persist the side we're leaving so each direction keeps its own fields.
    (void)applyChanges();
    editingReverse = reverse;
    applyActiveSide();
    loadFromScene();
    scrollY = 0.0f;
    status = editingReverse ? "Editing return path." : "Editing outbound path.";
    error.clear();
    return true;
}

void SceneExitRequirementsDialog::openForExit(
    const std::string& fromSceneId,
    const std::string& dir,
    const std::string& toId)
{
    resolveWireSides(fromSceneId, dir, toId);
    applyActiveSide();
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
    generateKind = 0;
    pendingTtsJobsPath.clear();
    stopPreviewVoice();
    open = false;
    waitMouseRelease = false;
    voiceMenuOpen = false;
    fieldContextOpen = false;
    itemSuggestOpen = false;
    editingReverse = false;
    reverseAvailable = false;
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
    blockedVariants.clear();
    selectedVariant = -1;
    if (graph == nullptr)
        return;
    const nlohmann::json req = graph->readExitRequirement(sceneId, direction);
    if (!req.is_object() || req.empty())
        return;
    requiresLightSource = req.value("requiresLightSource", false);
    requiresRoomPurchasedToday = req.value("requiresRoomPurchasedToday", false);
    // Prefer multi-item gates (e.g. mining_pick + crampons) as a comma list.
    if (req.contains("requiresInventoryItems") && req["requiresInventoryItems"].is_array())
    {
        std::string joined;
        for (const auto& entry : req["requiresInventoryItems"])
        {
            if (!entry.is_string())
                continue;
            const std::string id = entry.get<std::string>();
            if (id.empty())
                continue;
            if (!joined.empty())
                joined += ", ";
            joined += id;
        }
        requiresInventoryItem = joined;
    }
    if (requiresInventoryItem.empty())
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
    const nlohmann::json* variantsJson = nullptr;
    if (req.contains("blockedVariants") && req["blockedVariants"].is_array())
        variantsJson = &req["blockedVariants"];
    else if (req.contains("blocked_variants") && req["blocked_variants"].is_array())
        variantsJson = &req["blocked_variants"];
    if (variantsJson != nullptr)
    {
        int idx = 0;
        for (const auto& entry : *variantsJson)
        {
            if (!entry.is_object())
                continue;
            ExitBlockedVariantEdit v;
            v.when = entry.value("when", "");
            v.details = entry.value("details", entry.value("blockedDetails", ""));
            const nlohmann::json* bag = nullptr;
            if (entry.contains("tts") && entry["tts"].is_object())
                bag = &entry["tts"];
            else if (entry.contains("blockedTts") && entry["blockedTts"].is_object())
                bag = &entry["blockedTts"];
            if (bag != nullptr)
            {
                v.ttsText = bag->value("ttsText", bag->value("text", ""));
                v.ttsVoice = normalizeVoiceId(
                    bag->value("ttsVoice", bag->value("voice", "leo")));
                v.ttsAudio = bag->value("ttsAudio", defaultVariantAudioPath(idx));
            }
            else
            {
                v.ttsVoice = "leo";
                v.ttsAudio = defaultVariantAudioPath(idx);
            }
            blockedVariants.push_back(v);
            ++idx;
        }
    }
}

std::string SceneExitRequirementsDialog::inventoryItemTokenPrefix() const
{
    // Autocomplete applies to the token after the last comma.
    const std::string& s = requiresInventoryItem;
    const size_t comma = s.find_last_of(",;\n");
    std::string token = (comma == std::string::npos) ? s : s.substr(comma + 1);
    size_t a = 0;
    while (a < token.size()
           && (token[a] == ' ' || token[a] == '\t' || token[a] == '\r'))
        ++a;
    return token.substr(a);
}

void SceneExitRequirementsDialog::applyInventoryItemSuggestion(const std::string& itemId)
{
    if (itemId.empty())
        return;
    const std::string& s = requiresInventoryItem;
    const size_t comma = s.find_last_of(",;\n");
    if (comma == std::string::npos)
        requiresInventoryItem = itemId;
    else
    {
        std::string head = s.substr(0, comma + 1);
        // Keep a single space after the separator when present.
        if (head.empty() || (head.back() != ' ' && head.back() != '\t'))
            head.push_back(' ');
        requiresInventoryItem = head + itemId;
    }
    itemSuggestOpen = false;
    if (blockBadge == "auto")
        suggestBadgeFromGates();
}

std::vector<std::string> SceneExitRequirementsDialog::inventoryItemSuggestions(int maxCount) const
{
    std::vector<std::string> out;
    if (docs == nullptr || maxCount <= 0)
        return out;
    if (!docs->itemsLoaded)
        const_cast<DocumentWorkspace*>(docs)->loadItemsDocument();
    const std::string prefix = inventoryItemTokenPrefix();
    std::string prefixLower = prefix;
    for (char& ch : prefixLower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const std::vector<std::string> ids = docs->itemIds();
    for (const std::string& id : ids)
    {
        if (prefixLower.empty())
        {
            out.push_back(id);
        }
        else
        {
            std::string idLower = id;
            for (char& ch : idLower)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (idLower.rfind(prefixLower, 0) != 0 && idLower.find(prefixLower) == std::string::npos)
                continue;
            // Prefer prefix matches first — collect all then sort below.
            out.push_back(id);
        }
        if (static_cast<int>(out.size()) >= maxCount * 3)
            break;
    }
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        std::string al = a;
        std::string bl = b;
        for (char& ch : al)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (char& ch : bl)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const bool ap = !prefixLower.empty() && al.rfind(prefixLower, 0) == 0;
        const bool bp = !prefixLower.empty() && bl.rfind(prefixLower, 0) == 0;
        if (ap != bp)
            return ap;
        return al < bl;
    });
    if (static_cast<int>(out.size()) > maxCount)
        out.resize(static_cast<size_t>(maxCount));
    return out;
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
    {
        // Split "a, b" into requiresInventoryItems; a single id stays singular.
        std::vector<std::string> itemIds;
        std::string cur;
        for (char ch : requiresInventoryItem)
        {
            if (ch == ',' || ch == ';' || ch == '\n')
            {
                // trim
                size_t a = 0;
                while (a < cur.size()
                       && (cur[a] == ' ' || cur[a] == '\t' || cur[a] == '\r'))
                    ++a;
                size_t b = cur.size();
                while (b > a
                       && (cur[b - 1] == ' ' || cur[b - 1] == '\t' || cur[b - 1] == '\r'))
                    --b;
                if (b > a)
                    itemIds.push_back(cur.substr(a, b - a));
                cur.clear();
            }
            else
                cur.push_back(ch);
        }
        size_t a = 0;
        while (a < cur.size() && (cur[a] == ' ' || cur[a] == '\t' || cur[a] == '\r'))
            ++a;
        size_t b = cur.size();
        while (b > a && (cur[b - 1] == ' ' || cur[b - 1] == '\t' || cur[b - 1] == '\r'))
            --b;
        if (b > a)
            itemIds.push_back(cur.substr(a, b - a));

        if (itemIds.size() >= 2)
            req["requiresInventoryItems"] = itemIds;
        else if (itemIds.size() == 1)
            req["requiresInventoryItem"] = itemIds.front();
    }
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
    if (!blockedVariants.empty())
    {
        nlohmann::json arr = nlohmann::json::array();
        for (size_t i = 0; i < blockedVariants.size(); ++i)
        {
            const ExitBlockedVariantEdit& v = blockedVariants[i];
            if (v.when.empty() && v.details.empty() && v.ttsText.empty() && v.ttsAudio.empty())
                continue;
            nlohmann::json entry = nlohmann::json::object();
            if (!v.when.empty())
                entry["when"] = v.when;
            if (!v.details.empty())
                entry["details"] = v.details;
            if (!v.ttsText.empty() || !v.ttsAudio.empty())
            {
                nlohmann::json bag = nlohmann::json::object();
                bag["tts"] = !v.ttsText.empty();
                bag["ttsText"] = v.ttsText;
                bag["ttsVoice"] = v.ttsVoice.empty() ? "leo" : v.ttsVoice;
                bag["ttsAudio"] =
                    v.ttsAudio.empty() ? defaultVariantAudioPath(static_cast<int>(i))
                                       : v.ttsAudio;
                entry["tts"] = bag;
            }
            arr.push_back(entry);
        }
        if (!arr.empty())
            req["blockedVariants"] = arr;
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
    case 5:
        if (selectedVariant >= 0
            && selectedVariant < static_cast<int>(blockedVariants.size()))
            return &blockedVariants[static_cast<size_t>(selectedVariant)].when;
        return nullptr;
    case 6:
        if (selectedVariant >= 0
            && selectedVariant < static_cast<int>(blockedVariants.size()))
            return &blockedVariants[static_cast<size_t>(selectedVariant)].details;
        return nullptr;
    case 7:
        if (selectedVariant >= 0
            && selectedVariant < static_cast<int>(blockedVariants.size()))
            return &blockedVariants[static_cast<size_t>(selectedVariant)].ttsText;
        return nullptr;
    default:
        return nullptr;
    }
}

void SceneExitRequirementsDialog::typeIntoFocusedField()
{
    std::string* buffer = focusedString();
    if (buffer == nullptr)
        return;
    const bool multiline =
        (focusField == 2 || focusField == 3 || focusField == 6 || focusField == 7);

    // Tab accepts the top inventory autocomplete suggestion (#47).
    if (focusField == 0 && itemSuggestOpen && IsKeyPressed(KEY_TAB))
    {
        const std::vector<std::string> suggestions = inventoryItemSuggestions(8);
        if (!suggestions.empty())
            applyInventoryItemSuggestion(suggestions.front());
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp == '\n' || cp == '\r')
        {
            if (multiline)
                buffer->push_back('\n');
        }
        else if (cp >= 32)
        {
            insertUtf8(*buffer, cp);
            if (focusField == 0)
                itemSuggestOpen = true;
        }
        cp = GetCharPressed();
    }
    // Enter is a key event — not always in GetCharPressed (#52).
    if (multiline
        && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
            || IsKeyPressedRepeat(KEY_ENTER) || IsKeyPressedRepeat(KEY_KP_ENTER)))
        buffer->push_back('\n');
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        backspaceUtf8(*buffer);
        if (focusField == 0)
            itemSuggestOpen = true;
    }
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            buffer->append(clip);
            if (focusField == 0)
                itemSuggestOpen = true;
        }
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

void SceneExitRequirementsDialog::startTtsDialogGenerate()
{
    if (docs == nullptr || generateBusy.load())
        return;
    // Prefer current TTS field when present (#38); else blocked notebook prose.
    const std::string source =
        sceneTtsTextHasWord(blockedTtsText) ? blockedTtsText : blockedDetails;
    if (!sceneTtsTextHasWord(source))
    {
        error = "Add blocked details (or TTS text) before Generate TTS dialog.";
        return;
    }
    if (effectiveApiKey().empty())
    {
        error = "Paste an xAI API key (field below) before Generate TTS dialog.";
        return;
    }

    const std::string styleBlock = formatGenerationStyleBlock(
        loadGenerationStyleFilter(docs->resourceDir));
    const char* ttsEmbellishRules =
        "Return ONLY the speakable text (no markdown fences, no commentary). "
        "Do NOT rewrite plot, add new facts, or change meaning. Allowed edits only: "
        "spelling fixes, grammar fixes, and light TTS life-like markup "
        "([pause], [long-pause], [sigh], [laugh], <whisper>, <soft>, <emphasis>, "
        "<slow>, <fast>, {{voice:ID}}...{{/voice}} for quoted speech). "
        "Keep 1890s Highline Ridge tone. Narrator prose stays unwrapped.\n";
    const std::string prompt =
        std::string(
            "Prepare the following blocked-exit text as spoken Timberline TTS "
            "narration (player tried a gated MOVE and is stopped). ")
        + ttsEmbellishRules + styleBlock + "\nSOURCE:\n" + source;

    const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
    const std::string authoringDir =
        pathJoin(pathJoin(root, "resources"), ".authoring");
#if !defined(_WIN32)
    const std::string mkdirCmd = "mkdir -p '" + authoringDir + "'";
    std::system(mkdirCmd.c_str());
#endif
    const std::string outRel = "resources/.authoring/" + sceneId + "_blocked_"
        + direction + "_tts.txt";
    pendingTtsJobsPath = pathJoin(
        authoringDir, sceneId + "_blocked_" + direction + "_tts_jobs.json");

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = sceneId;
    jobsRoot["itemId"] = sceneId;
    jobsRoot["kind"] = "exit_blocked_tts";
    jobsRoot["jobs"] = nlohmann::json::array(
        {nlohmann::json{
            {"type", "generate_scene_description_tts_text"},
            {"prompt", prompt},
            {"outPath", outRel},
            {"action", "blocked_tts"},
            {"sourceText", source}}});
    {
        std::ofstream out(pendingTtsJobsPath.c_str());
        if (!out)
        {
            error = "Failed to write TTS jobs file.";
            pendingTtsJobsPath.clear();
            return;
        }
        out << jobsRoot.dump(2);
    }

    generateCancel.store(false);
    generateBusy = true;
    generateKind = 1;
    generateResultPending = false;
    status = "Generating blocked TTS dialog…";
    error.clear();
    const std::string keySnap = effectiveApiKey();
    const std::string jobsSnap = pendingTtsJobsPath;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    if (generateThread.joinable())
        generateThread.join();
    generateThread = std::thread([this, keySnap, jobsSnap, assetRoot, resourceDir]() {
        const std::string msg = runSceneAuthoringAiJobsFile(
            assetRoot, resourceDir, jobsSnap, keySnap, &generateCancel);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
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
    generateKind = 2;
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
    const int kind = generateKind;
    status = generateResultStatus;
    generateResultPending = false;
    generateBusy = false;
    generateKind = 0;

    if (kind == 1 && !pendingTtsJobsPath.empty())
    {
        try
        {
            std::ifstream jobsIn(pendingTtsJobsPath.c_str());
            if (jobsIn)
            {
                nlohmann::json root;
                jobsIn >> root;
                if (root.contains("jobs") && root["jobs"].is_array())
                {
                    for (const auto& job : root["jobs"])
                    {
                        if (!job.is_object())
                            continue;
                        std::string text = job.value("resultText", "");
                        if (text.empty())
                        {
                            const std::string outRel = job.value("outPath", "");
                            if (!outRel.empty() && docs != nullptr)
                            {
                                const std::string rootDir =
                                    docs->assetRoot.empty() ? "." : docs->assetRoot;
                                std::ifstream tf(pathJoin(rootDir, outRel).c_str());
                                if (tf)
                                    text.assign(
                                        (std::istreambuf_iterator<char>(tf)),
                                        std::istreambuf_iterator<char>());
                            }
                        }
                        while (!text.empty()
                               && (text.back() == '\n' || text.back() == '\r'
                                   || text.back() == ' ' || text.back() == '\t'))
                            text.pop_back();
                        if (!text.empty())
                        {
                            blockedTtsText = text;
                            if (blockedTtsAudio.empty())
                                blockedTtsAudio = defaultBlockedAudioPath();
                            (void)applyChanges();
                            status = "Blocked TTS dialog ready — Generate Voice next.";
                        }
                    }
                }
            }
        }
        catch (const nlohmann::json::exception&)
        {
        }
        pendingTtsJobsPath.clear();
        return;
    }

    if (kind == 2)
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
    if (lastScrollClip.width > 1.0f && CheckCollisionPointRec(GetMousePosition(), lastScrollClip))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            const float maxScroll =
                std::max(0.0f, lastContentH - lastScrollClip.height);
            scrollY = std::clamp(scrollY - wheel * 28.0f, 0.0f, maxScroll);
        }
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
    const float dialogH = std::min(680.0f, screenH - 24.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f, (screenH - dialogH) * 0.5f, dialogW, dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 16.0f;
    const float fieldX = dialog.x + pad;
    const float fieldW = dialog.width - pad * 2.0f;
    float y = dialog.y + pad;

    DrawTextEx(bold, "Exit requirements", {fieldX, y}, kFontHeading, 1.0f, kTextPrimary);
    y += 28.0f;

    // Direction slider: flip between outbound / return path.
    const int activeSide = editingReverse ? 1 : 0;
    const std::string pathLabel = sideFrom[activeSide].empty()
        ? "(no path)"
        : (sideFrom[activeSide] + " -> " + sideTo[activeSide]);
    const float sliderW = 52.0f;
    const float sliderH = 24.0f;
    directionSliderRect = {fieldX, y, sliderW, sliderH};
    const bool sliderEnabled = reverseAvailable && !busy;
    DrawRectangleRounded(directionSliderRect, 0.5f, 6, Color{28, 26, 36, 255});
    DrawRectangleRoundedLines(directionSliderRect, 0.5f, 6, kPanelInnerEdge);
    const float knobSize = 18.0f;
    const float knobX = editingReverse
        ? (directionSliderRect.x + directionSliderRect.width - knobSize - 3.0f)
        : (directionSliderRect.x + 3.0f);
    const Rectangle knob = {
        knobX,
        directionSliderRect.y + (sliderH - knobSize) * 0.5f,
        knobSize,
        knobSize};
    DrawRectangleRounded(
        knob,
        0.5f,
        6,
        sliderEnabled ? kPanelAccent : Color{70, 66, 80, 255});
    if (canClick && sliderEnabled && CheckCollisionPointRec(mouse, directionSliderRect))
        setEditingReverse(!editingReverse);

    const float labelX = directionSliderRect.x + sliderW + 10.0f;
    const float labelMaxW = fieldX + fieldW - labelX;
    const std::string pathShown =
        ellipsizeUi(font, pathLabel, kFontSmall, labelMaxW);
    DrawTextEx(
        font, pathShown.c_str(), {labelX, y + 4.0f}, kFontSmall, 1.0f, kTextPrimary);
    y += 28.0f;
    if (!reverseAvailable)
    {
        DrawTextEx(
            font,
            "No return exit on the other scene — slider disabled.",
            {fieldX, y},
            kFontTiny,
            1.0f,
            kTextMuted);
        y += 16.0f;
    }

    // Separator under direction controls (#47).
    y += 4.0f;
    DrawRectangle(
        static_cast<int>(fieldX),
        static_cast<int>(y),
        static_cast<int>(fieldW),
        1,
        kPanelInnerEdge);
    y += 10.0f;

    const float footerH = 52.0f;
    const Rectangle scrollClip = {
        fieldX, y, fieldW, dialog.y + dialogH - footerH - y};
    lastScrollClip = scrollClip;

    // Content height estimate for scrolling (drawn with yOffset).
    float contentY = 0.0f;
    auto advance = [&](float dy) { contentY += dy; };

    // First pass sizes content; second would be wasteful — build once with scroll.
    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);

    auto rowY = [&]() { return scrollClip.y + contentY - scrollY; };

    // Row toggles: compact slider + description (#47).
    auto rowToggle = [&](const char* description, bool& value) {
        const float rowH = 28.0f;
        const float sw = 44.0f;
        const float sh = 22.0f;
        const Rectangle track = {fieldX, rowY() + 3.0f, sw, sh};
        DrawRectangleRounded(track, 0.5f, 6, Color{28, 26, 36, 255});
        DrawRectangleRoundedLines(track, 0.5f, 6, kPanelInnerEdge);
        const float ksz = 16.0f;
        const float kx = value ? (track.x + track.width - ksz - 3.0f) : (track.x + 3.0f);
        DrawRectangleRounded(
            {kx, track.y + (sh - ksz) * 0.5f, ksz, ksz},
            0.5f,
            6,
            value ? kPanelAccent : Color{70, 66, 80, 255});
        DrawTextEx(
            font,
            description,
            {track.x + sw + 10.0f, rowY() + 5.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        const Rectangle hit = {fieldX, rowY(), fieldW, rowH};
        if (canClick && !busy && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, hit))
        {
            value = !value;
            if (blockBadge == "auto")
                suggestBadgeFromGates();
        }
        advance(rowH + 4.0f);
    };
    rowToggle("Exit requires a light source (any inventory lightSource item)", requiresLightSource);
    rowToggle(
        "Exit requires a room purchased today (saloon lodging)",
        requiresRoomPurchasedToday);
    advance(6.0f);

    // Inventory + story flag stacked full-width (was side-by-side and confusing).
    DrawTextEx(
        font,
        "Requires inventory item id(s) — type to autocomplete; comma-separate if all needed",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(16.0f);

    Rectangle itemField = {fieldX, rowY(), fieldW, 28.0f};
    itemFieldRect = itemField;
    DrawRectangleRec(itemField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        itemField, focusField == 0 ? 2.0f : 1.0f, focusField == 0 ? kPanelBorder : kPanelInnerEdge);
    drawClippedFieldText(
        font,
        itemField,
        requiresInventoryItem,
        "(none — e.g. key_ring)",
        kFontSmall,
        requiresInventoryItem.empty() ? kTextMuted : kTextPrimary);
    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, itemField))
    {
        focusField = 0;
        itemSuggestOpen = true;
        if (docs != nullptr && !docs->itemsLoaded)
            docs->loadItemsDocument();
    }
    advance(36.0f);

    DrawTextEx(
        font,
        "Requires story flag (optional unlock-once; e.g. saloon_service_hall:supply_unlocked)",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(16.0f);

    Rectangle flagField = {fieldX, rowY(), fieldW, 28.0f};
    DrawRectangleRec(flagField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        flagField, focusField == 1 ? 2.0f : 1.0f, focusField == 1 ? kPanelBorder : kPanelInnerEdge);
    drawClippedFieldText(
        font,
        flagField,
        requiresStoryFlag,
        "(none)",
        kFontSmall,
        requiresStoryFlag.empty() ? kTextMuted : kTextPrimary);
    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, flagField))
    {
        focusField = 1;
        itemSuggestOpen = false;
    }
    advance(40.0f);

    Rectangle badgeBtn = {fieldX, rowY(), 220.0f, 28.0f};
    drawEditorButton(
        font, badgeBtn, ("Block badge: " + blockBadge).c_str(), true, !busy);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, badgeBtn))
        cycleBadge();
    Rectangle suggestBtn = {badgeBtn.x + 230.0f, rowY(), 140.0f, 28.0f};
    drawEditorButton(font, suggestBtn, "Suggest badge", false, !busy);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, suggestBtn))
        suggestBadgeFromGates();
    advance(40.0f);

    DrawTextEx(
        font, "Blocked details (notebook)", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
    advance(16.0f);
    Rectangle detailsField = {fieldX, rowY(), fieldW, 90.0f};
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
    // Re-enter outer scroll scissor (nested End cleared it).
    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, detailsField))
        focusField = 2;
    if (parchment != nullptr && editorMousePressed(MOUSE_BUTTON_RIGHT)
        && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, detailsField))
    {
        fieldContextOpen = true;
        fieldContextTarget = 2;
        fieldContextRect = {mouse.x, mouse.y, 160.0f, 28.0f};
    }
    advance(100.0f);

    DrawTextEx(font, "Blocked TTS text", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
    advance(16.0f);
    Rectangle ttsField = {fieldX, rowY(), fieldW, 90.0f};
    DrawRectangleRec(ttsField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        ttsField, focusField == 3 ? 2.0f : 1.0f, focusField == 3 ? kPanelBorder : kPanelInnerEdge);
    BeginScissorMode(
        (int)ttsField.x + 2,
        (int)ttsField.y + 2,
        (int)ttsField.width - 4,
        (int)ttsField.height - 4);
    DrawTextEx(
        font,
        blockedTtsText.empty() ? "(empty — paste or edit spoken markup)" : blockedTtsText.c_str(),
        {ttsField.x + 8.0f, ttsField.y + 6.0f},
        kFontSmall,
        1.0f,
        blockedTtsText.empty() ? kTextMuted : kTextPrimary);
    EndScissorMode();
    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, ttsField))
        focusField = 3;
    if (parchment != nullptr && editorMousePressed(MOUSE_BUTTON_RIGHT)
        && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, ttsField))
    {
        fieldContextOpen = true;
        fieldContextTarget = 3;
        fieldContextRect = {mouse.x, mouse.y, 160.0f, 28.0f};
    }
    advance(100.0f);

    const float btnW = (fieldW - 8.0f) * 0.5f;
    const float btnH = 28.0f;
    voiceBtnRect = {fieldX, rowY(), btnW, btnH};
    Rectangle genTtsBtn = {fieldX + btnW + 8.0f, rowY(), btnW, btnH};
    const bool canTtsDialog =
        !busy && !effectiveApiKey().empty()
        && (sceneTtsTextHasWord(blockedTtsText) || sceneTtsTextHasWord(blockedDetails));
    drawEditorButton(
        font,
        voiceBtnRect,
        ("Voice: " + (blockedTtsVoice.empty() ? "leo" : blockedTtsVoice)).c_str(),
        true,
        !busy);
    drawEditorButton(
        font,
        genTtsBtn,
        (busy && generateKind == 1) ? "Working..." : "Generate TTS dialog",
        true,
        canTtsDialog);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, voiceBtnRect))
        voiceMenuOpen = !voiceMenuOpen;
    if (canClick && canTtsDialog && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, genTtsBtn))
        startTtsDialogGenerate();
    advance(btnH + 8.0f);

    Rectangle genVoiceBtn = {fieldX, rowY(), btnW, btnH};
    Rectangle previewBtn = {fieldX + btnW + 8.0f, rowY(), btnW, btnH};
    const bool canGen =
        !busy && sceneTtsTextHasWord(blockedTtsText) && !effectiveApiKey().empty();
    const bool canPrev = !busy && blockedAudioExists();
    drawEditorButton(
        font,
        genVoiceBtn,
        (busy && generateKind == 2) ? "Working..." : "Generate Voice",
        true,
        canGen);
    drawEditorButton(
        font,
        previewBtn,
        previewVoicePlaying ? "Stop preview" : "Preview voice",
        true,
        canPrev);
    if (canClick && canGen && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, genVoiceBtn))
        startVoiceRefresh();
    if (canClick && canPrev && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, previewBtn))
    {
        if (previewVoicePlaying)
            stopPreviewVoice();
        else
            startPreviewVoice();
    }
    advance(btnH + 12.0f);

    // --- Blocked variants (conditional bags) ---
    DrawTextEx(font, "Blocked variants", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
    advance(16.0f);
    DrawTextEx(
        font,
        "First matching when wins. Empty when = default (place last). "
        "Example: item:padlock_key:in_inventory",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(16.0f);

    Rectangle addVarBtn = {fieldX, rowY(), 110.0f, 26.0f};
    Rectangle remVarBtn = {fieldX + 118.0f, rowY(), 120.0f, 26.0f};
    drawEditorButton(font, addVarBtn, "Add variant", true, !busy);
    drawEditorButton(
        font,
        remVarBtn,
        "Remove",
        false,
        !busy && selectedVariant >= 0
            && selectedVariant < static_cast<int>(blockedVariants.size()));
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, addVarBtn))
        addBlockedVariant();
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, remVarBtn)
        && selectedVariant >= 0)
        removeSelectedVariant();
    advance(32.0f);

    const float rowH = 22.0f;
    for (size_t vi = 0; vi < blockedVariants.size(); ++vi)
    {
        const ExitBlockedVariantEdit& v = blockedVariants[vi];
        Rectangle row = {fieldX, rowY(), fieldW, rowH};
        const bool sel = selectedVariant == static_cast<int>(vi);
        if (sel)
            DrawRectangleRec(row, Color{50, 44, 62, 255});
        else if (CheckCollisionPointRec(mouse, row))
            DrawRectangleRec(row, Color{36, 32, 44, 200});
        const std::string label = ellipsizeUi(
            font,
            "#" + std::to_string(vi) + "  when: "
                + (v.when.empty() ? "(default)" : v.when),
            kFontSmall,
            fieldW - 12.0f);
        DrawTextEx(
            font,
            label.c_str(),
            {row.x + 6.0f, row.y + 3.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, row))
        {
            selectedVariant = static_cast<int>(vi);
            focusField = 5;
        }
        advance(rowH);
    }
    if (blockedVariants.empty())
    {
        DrawTextEx(
            font,
            "(none — base blocked details/TTS only)",
            {fieldX, rowY()},
            kFontTiny,
            1.0f,
            kTextMuted);
        advance(18.0f);
    }

    if (selectedVariant >= 0
        && selectedVariant < static_cast<int>(blockedVariants.size()))
    {
        ExitBlockedVariantEdit& v =
            blockedVariants[static_cast<size_t>(selectedVariant)];
        advance(8.0f);
        DrawTextEx(
            font,
            ("Variant #" + std::to_string(selectedVariant) + " when").c_str(),
            {fieldX, rowY()},
            kFontTiny,
            1.0f,
            kTextMuted);
        advance(16.0f);
        Rectangle whenField = {fieldX, rowY(), fieldW, 28.0f};
        DrawRectangleRec(whenField, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            whenField,
            focusField == 5 ? 2.0f : 1.0f,
            focusField == 5 ? kPanelBorder : kPanelInnerEdge);
        drawClippedFieldText(
            font,
            whenField,
            v.when,
            "(empty = always / default)",
            kFontSmall,
            v.when.empty() ? kTextMuted : kTextPrimary);
        BeginScissorMode(
            (int)scrollClip.x,
            (int)scrollClip.y,
            (int)scrollClip.width,
            (int)scrollClip.height);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, whenField))
            focusField = 5;
        advance(36.0f);

        DrawTextEx(font, "Variant details", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
        advance(16.0f);
        Rectangle vDetails = {fieldX, rowY(), fieldW, 70.0f};
        DrawRectangleRec(vDetails, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            vDetails,
            focusField == 6 ? 2.0f : 1.0f,
            focusField == 6 ? kPanelBorder : kPanelInnerEdge);
        BeginScissorMode(
            (int)vDetails.x + 2,
            (int)vDetails.y + 2,
            (int)vDetails.width - 4,
            (int)vDetails.height - 4);
        DrawTextEx(
            font,
            v.details.empty() ? "(empty)" : v.details.c_str(),
            {vDetails.x + 8.0f, vDetails.y + 6.0f},
            kFontSmall,
            1.0f,
            v.details.empty() ? kTextMuted : kTextPrimary);
        EndScissorMode();
        BeginScissorMode(
            (int)scrollClip.x,
            (int)scrollClip.y,
            (int)scrollClip.width,
            (int)scrollClip.height);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, vDetails))
            focusField = 6;
        advance(80.0f);

        DrawTextEx(font, "Variant TTS text", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
        advance(16.0f);
        Rectangle vTts = {fieldX, rowY(), fieldW, 70.0f};
        DrawRectangleRec(vTts, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            vTts,
            focusField == 7 ? 2.0f : 1.0f,
            focusField == 7 ? kPanelBorder : kPanelInnerEdge);
        BeginScissorMode(
            (int)vTts.x + 2, (int)vTts.y + 2, (int)vTts.width - 4, (int)vTts.height - 4);
        DrawTextEx(
            font,
            v.ttsText.empty() ? "(empty — no condition tags in TTS bake text)"
                              : v.ttsText.c_str(),
            {vTts.x + 8.0f, vTts.y + 6.0f},
            kFontSmall,
            1.0f,
            v.ttsText.empty() ? kTextMuted : kTextPrimary);
        EndScissorMode();
        BeginScissorMode(
            (int)scrollClip.x,
            (int)scrollClip.y,
            (int)scrollClip.width,
            (int)scrollClip.height);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, vTts))
            focusField = 7;
        advance(80.0f);
        DrawTextEx(
            font,
            "Save, then Generate Voice (scene refresh) to bake variant clips.",
            {fieldX, rowY()},
            kFontTiny,
            1.0f,
            kTextMuted);
        advance(18.0f);
        (void)v;
    }

    DrawTextEx(font, "xAI API key (session)", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
    advance(16.0f);
    Rectangle keyField = {fieldX, rowY(), fieldW, 28.0f};
    DrawRectangleRec(keyField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        keyField, focusField == 4 ? 2.0f : 1.0f, focusField == 4 ? kPanelBorder : kPanelInnerEdge);
    const std::string keyRaw =
        sessionApiKey.empty() ? "(uses editor prefs if set)"
                              : std::string(sessionApiKey.size(), '*');
    drawClippedFieldText(
        font,
        keyField,
        keyRaw,
        "(uses editor prefs if set)",
        kFontSmall,
        sessionApiKey.empty() ? kTextMuted : kTextPrimary);
    // drawClippedFieldText ends scissor — restore scroll clip
    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, keyField))
        focusField = 4;
    advance(36.0f);

    lastContentH = contentY;
    const float maxScroll = std::max(0.0f, lastContentH - scrollClip.height);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    EndScissorMode();

    // Scrollbar when content overflows.
    if (maxScroll > 1.0f)
    {
        const float trackX = dialog.x + dialog.width - 10.0f;
        const Rectangle track = {trackX, scrollClip.y, 4.0f, scrollClip.height};
        DrawRectangleRec(track, Color{40, 36, 48, 255});
        const float thumbH = std::max(
            24.0f, scrollClip.height * (scrollClip.height / lastContentH));
        const float thumbY =
            scrollClip.y + (scrollClip.height - thumbH) * (scrollY / maxScroll);
        DrawRectangleRec({trackX, thumbY, 4.0f, thumbH}, kPanelAccent);
    }

    // Footer
    const float btnY = dialog.y + dialogH - 44.0f;
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

    const float msgX = cancelBtn.x + 120.0f;
    const float msgMaxW = dialog.x + dialog.width - pad - msgX;
    if (!error.empty())
    {
        const std::string shown = ellipsizeUi(font, error, kFontTiny, msgMaxW);
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 8.0f}, kFontTiny, 1.0f, Color{220, 90, 80, 255});
    }
    else if (!status.empty())
    {
        const std::string shown = ellipsizeUi(font, status, kFontTiny, msgMaxW);
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 8.0f}, kFontTiny, 1.0f, Color{120, 180, 120, 255});
    }

    // Inventory item autocomplete dropdown (drawn above footer / outside scissor).
    if (itemSuggestOpen && focusField == 0)
    {
        const std::vector<std::string> suggestions = inventoryItemSuggestions(10);
        if (!suggestions.empty())
        {
            const float rowH = 22.0f;
            const float menuH = rowH * static_cast<float>(suggestions.size()) + 6.0f;
            itemSuggestRect = {
                itemFieldRect.x,
                itemFieldRect.y + itemFieldRect.height + 2.0f,
                itemFieldRect.width,
                menuH};
            // Keep on screen if near bottom of dialog.
            if (itemSuggestRect.y + itemSuggestRect.height > dialog.y + dialogH - footerH)
                itemSuggestRect.y = itemFieldRect.y - itemSuggestRect.height - 2.0f;
            DrawRectangleRec(itemSuggestRect, Color{36, 32, 44, 255});
            DrawRectangleLinesEx(itemSuggestRect, 1.0f, kPanelBorder);
            float sy = itemSuggestRect.y + 3.0f;
            for (const std::string& id : suggestions)
            {
                Rectangle row = {
                    itemSuggestRect.x + 2.0f,
                    sy,
                    itemSuggestRect.width - 4.0f,
                    rowH - 2.0f};
                if (CheckCollisionPointRec(mouse, row))
                {
                    DrawRectangleRec(row, Color{60, 54, 72, 220});
                    if (canClick)
                        applyInventoryItemSuggestion(id);
                }
                std::string label = id;
                if (docs != nullptr)
                {
                    const nlohmann::json* item = docs->itemJson(id);
                    if (item != nullptr && item->is_object())
                    {
                        const std::string name = item->value("name", "");
                        if (!name.empty() && name != id)
                            label = name + "  (" + id + ")";
                    }
                }
                DrawTextEx(
                    font,
                    ellipsizeUi(font, label, kFontSmall, row.width - 12.0f).c_str(),
                    {row.x + 6.0f, row.y + 2.0f},
                    kFontSmall,
                    1.0f,
                    kTextPrimary);
                sy += rowH;
            }
            DrawTextEx(
                font,
                "Tab = accept top match",
                {itemSuggestRect.x + 8.0f, itemSuggestRect.y + itemSuggestRect.height + 2.0f},
                kFontTiny,
                1.0f,
                kTextMuted);
        }
        if (canClick && !CheckCollisionPointRec(mouse, itemFieldRect)
            && !CheckCollisionPointRec(mouse, itemSuggestRect))
            itemSuggestOpen = false;
    }
    else if (focusField != 0)
    {
        itemSuggestOpen = false;
    }

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
}

} // namespace timberline_editor
