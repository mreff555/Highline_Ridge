/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#include "XaiTtsClient.h"

#include "TextDigest.h"
#include "TtsContentValidator.h"
#include "TtsVoiceMarkup.h"
#include <ImageCompression.h>
#include <PlatformPath.h>
#include <nlohmann/json.hpp>
#include <raylib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

namespace timberline_engine
{

namespace
{

bool sourceResourcesTreeExists()
{
    return FileExists(pathJoin("..", "resources/scenes.json").c_str());
}

std::string runtimeResourcePath(const std::string& resourcePath)
{
    if (resourcePath.rfind("../", 0) == 0)
        return resourcePath.substr(3);

    return resourcePath;
}

bool mirrorBundleToSourceTree(const std::string& runtimeBundlePath)
{
    if (!sourceResourcesTreeExists())
        return true;

    const std::string sourceBundlePath = pathJoin("..", runtimeBundlePath);
    std::ifstream input(runtimeBundlePath.c_str(), std::ios::binary);
    if (!input.is_open())
        return false;

    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    input.close();
    if (bytes.empty())
        return false;

    if (!ensureParentDirectoryExists(sourceBundlePath))
        return false;

    return writeBinaryFile(sourceBundlePath, bytes.data(), bytes.size());
}

std::string curlExecutable()
{
#if defined(__APPLE__)
    return "/usr/bin/curl";
#else
    return "curl";
#endif
}

std::string trimWhitespace(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(start, end - start);
}

std::string flattenNarrationText(const std::string& value)
{
    std::string flattened;
    flattened.reserve(value.size());

    bool previousWasSpace = true;
    for (char character : value)
    {
        if (character == '\n' || character == '\r' || character == '\t')
            character = ' ';

        const bool isSpace = std::isspace(static_cast<unsigned char>(character)) != 0;
        if (isSpace)
        {
            if (!previousWasSpace)
                flattened.push_back(' ');
            previousWasSpace = true;
            continue;
        }

        flattened.push_back(character);
        previousWasSpace = false;
    }

    while (!flattened.empty() && flattened.back() == ' ')
        flattened.pop_back();

    return flattened;
}

std::string lookupStoredSha256(
    const nlohmann::json& node,
    const char* audioField)
{
    if (std::strcmp(audioField, "ttsAudio") == 0)
        return node.value("ttsTextSha256", "");
    if (std::strcmp(audioField, "ttsAfterAudio") == 0)
        return node.value("ttsAfterTextSha256", "");
    if (std::strcmp(audioField, "resumeTtsAudio") == 0)
        return node.value("resumeTtsTextSha256", "");
    if (std::strcmp(audioField, "ttsVariantAudio") == 0)
        return node.value("ttsVariantTextSha256", "");
    return "";
}

bool updateSha256InJsonTree(
    nlohmann::json& node,
    const std::string& audioPath,
    const std::string& textSha256)
{
    bool updated = false;

    if (node.is_object())
    {
        if (node.value("ttsAudio", "") == audioPath)
        {
            node["ttsTextSha256"] = textSha256;
            updated = true;
        }
        if (node.value("ttsAfterAudio", "") == audioPath)
        {
            node["ttsAfterTextSha256"] = textSha256;
            updated = true;
        }
        if (node.value("resumeTtsAudio", "") == audioPath)
        {
            node["resumeTtsTextSha256"] = textSha256;
            updated = true;
        }
        if (node.value("ttsVariantAudio", "") == audioPath)
        {
            node["ttsVariantTextSha256"] = textSha256;
            updated = true;
        }

        for (auto it = node.begin(); it != node.end(); ++it)
            updated = updateSha256InJsonTree(*it, audioPath, textSha256) || updated;
    }
    else if (node.is_array())
    {
        for (nlohmann::json& child : node)
            updated = updateSha256InJsonTree(child, audioPath, textSha256) || updated;
    }

    return updated;
}

bool updateAudioSegmentsInJsonTree(
    nlohmann::json& node,
    const std::string& audioPath,
    const std::vector<std::string>& segmentPaths)
{
    bool updated = false;

    if (node.is_object())
    {
        if (node.value("ttsAudio", "") == audioPath)
        {
            if (segmentPaths.size() > 1)
                node["ttsAudioSegments"] = segmentPaths;
            else if (node.contains("ttsAudioSegments"))
                node.erase("ttsAudioSegments");
            updated = true;
        }
        if (node.value("ttsAfterAudio", "") == audioPath)
        {
            if (segmentPaths.size() > 1)
                node["ttsAfterAudioSegments"] = segmentPaths;
            else if (node.contains("ttsAfterAudioSegments"))
                node.erase("ttsAfterAudioSegments");
            updated = true;
        }

        for (auto it = node.begin(); it != node.end(); ++it)
            updated = updateAudioSegmentsInJsonTree(*it, audioPath, segmentPaths) || updated;
    }
    else if (node.is_array())
    {
        for (nlohmann::json& child : node)
            updated = updateAudioSegmentsInJsonTree(child, audioPath, segmentPaths) || updated;
    }

    return updated;
}

bool bundleExistsAtResolvedPath(const std::string& resolvedAudioPath)
{
    return FileExists(compressedAssetPath(resolvedAudioPath).c_str());
}

bool allSegmentBundlesExist(
    const std::string& assetRoot,
    const std::string& baseAudioPath,
    const std::vector<std::string>& segmentPaths)
{
    for (const std::string& segmentPath : segmentPaths)
    {
        const std::vector<std::string> searchPaths =
            buildAssetSearchPaths(assetRoot, segmentPath);
        bool found = false;
        for (const std::string& path : searchPaths)
        {
            if (bundleExistsAtResolvedPath(path))
            {
                found = true;
                break;
            }
        }

        if (!found)
            return false;
    }

    return true;
}

void removeStaleTtsBundles(
    const std::string& assetRoot,
    const std::string& baseAudioPath)
{
    const std::vector<std::string> basePaths = buildAssetSearchPaths(assetRoot, baseAudioPath);
    for (const std::string& path : basePaths)
    {
        std::remove(compressedAssetPath(path).c_str());
        std::remove(path.c_str());
    }

    for (size_t segmentIndex = 0; segmentIndex < 16; ++segmentIndex)
    {
        const std::string segmentPath =
            buildSegmentAudioPath(baseAudioPath, segmentIndex, 999);
        const std::vector<std::string> segmentPaths =
            buildAssetSearchPaths(assetRoot, segmentPath);
        for (const std::string& path : segmentPaths)
        {
            std::remove(compressedAssetPath(path).c_str());
            std::remove(path.c_str());
        }
    }
}

bool writeJsonFileIfChanged(
    const std::string& path,
    nlohmann::json& root,
    bool changed)
{
    if (!changed)
        return true;

    std::ofstream out(path.c_str());
    if (!out.is_open())
        return false;

    out << root.dump(2);
    return out.good();
}

void addEntryIfPresent(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId,
    const std::string& fallbackText,
    const char* audioField,
    const char* textField,
    const char* voiceField,
    bool requiredFlag,
    const char* flagField,
    const nlohmann::json* sha256Node = nullptr)
{
    if (!node.is_object())
        return;

    if (requiredFlag)
    {
        const bool flagOn = node.value(flagField, false)
            || (std::string(flagField) == "tts" && node.value("enabled", false));
        if (!flagOn)
            return;
    }

    TtsVoiceEntry entry;
    entry.audioPath = node.value(audioField, "");
    entry.text = node.value(textField, "");
    if (entry.text.empty() && std::string(textField) == "ttsText")
        entry.text = node.value("text", "");
    entry.voiceId = node.value(voiceField, "");
    if (entry.voiceId.empty())
        entry.voiceId = node.value("voice", "");
    if (entry.voiceId.empty())
        entry.voiceId = defaultVoiceId;
    // Never silently invent a voice — empty means skip (policy should have supplied one).
    if (entry.voiceId.empty() || !isKnownBuiltinVoiceId(entry.voiceId))
        return;

    if (entry.text.empty())
        entry.text = flattenNarrationText(fallbackText);

    if (entry.audioPath.empty() || entry.text.empty())
        return;

    const nlohmann::json& sha256Source = sha256Node != nullptr ? *sha256Node : node;
    if (sha256Node != nullptr
        && node.value(audioField, "") == sha256Node->value("resumeTtsAudio", ""))
    {
        entry.storedTextSha256 = sha256Source.value("resumeTtsTextSha256", "");
    }
    else if (sha256Node != nullptr
        && node.value(audioField, "") == sha256Node->value("ttsVariantAudio", ""))
    {
        entry.storedTextSha256 = sha256Source.value("ttsVariantTextSha256", "");
    }
    else
    {
        entry.storedTextSha256 = lookupStoredSha256(sha256Source, audioField);
    }

    for (const TtsVoiceEntry& existing : entries)
    {
        if (existing.audioPath == entry.audioPath)
            return;
    }

    entries.push_back(entry);
}

void addPrimaryTtsEntry(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId,
    const std::string& fallbackText,
    const nlohmann::json* sha256Node = nullptr)
{
    addEntryIfPresent(
        entries,
        node,
        defaultVoiceId,
        fallbackText,
        "ttsAudio",
        "ttsText",
        "ttsVoice",
        true,
        "tts",
        sha256Node);
}

void addAfterTtsEntry(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId,
    const nlohmann::json* sha256Node = nullptr)
{
    addEntryIfPresent(
        entries,
        node,
        defaultVoiceId,
        "",
        "ttsAfterAudio",
        "ttsAfterText",
        "ttsAfterVoice",
        true,
        "ttsAfter",
        sha256Node);
}

void collectChoiceEntries(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& choices,
    const std::string& defaultVoiceId,
    const std::string& choiceIdFilter = "")
{
    if (!choices.is_array())
        return;

    for (const nlohmann::json& choice : choices)
    {
        const std::string choiceId = choice.value("id", "");
        const bool restrictToChoice = !choiceIdFilter.empty();
        if (restrictToChoice && choiceId != choiceIdFilter)
        {
            if (choice.contains("choices"))
                collectChoiceEntries(entries, choice["choices"], defaultVoiceId, choiceIdFilter);
            continue;
        }

        addPrimaryTtsEntry(entries, choice, defaultVoiceId, choice.value("response", ""));
        addAfterTtsEntry(entries, choice, defaultVoiceId);
        if (!restrictToChoice && choice.contains("choices"))
            collectChoiceEntries(entries, choice["choices"], defaultVoiceId);
        else if (restrictToChoice && choice.contains("choices"))
            collectChoiceEntries(entries, choice["choices"], defaultVoiceId, choiceIdFilter);
    }
}

void collectRandomLineEntries(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& lines,
    const std::string& defaultVoiceId,
    const std::string& lineIdFilter = "",
    const std::string& choiceIdFilter = "")
{
    if (!lines.is_array())
        return;

    for (const nlohmann::json& line : lines)
    {
        if (!lineIdFilter.empty() && line.value("id", "") != lineIdFilter)
            continue;

        addPrimaryTtsEntry(entries, line, defaultVoiceId, line.value("text", ""));
        addAfterTtsEntry(entries, line, defaultVoiceId);
        collectChoiceEntries(
            entries,
            line.value("choices", nlohmann::json::array()),
            defaultVoiceId,
            choiceIdFilter);
    }
}

void collectSceneNarrativeEntries(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId)
{
    if (!node.is_object())
        return;

    if (node.contains("descriptionTts") && node["descriptionTts"].is_object())
    {
        addPrimaryTtsEntry(
            entries,
            node["descriptionTts"],
            defaultVoiceId,
            node.value("description", ""));
    }

    if (node.contains("examineTts") && node["examineTts"].is_object())
    {
        addPrimaryTtsEntry(
            entries,
            node["examineTts"],
            defaultVoiceId,
            node.value("examineDetails", ""));
    }

    if (node.contains("wakeTts") && node["wakeTts"].is_object())
    {
        addPrimaryTtsEntry(
            entries,
            node["wakeTts"],
            defaultVoiceId,
            node.value("wakeNarrative", ""));
    }
}

enum class TtsRefreshTargetKind
{
    All,
    Scene,
    Phase,
    RandomLine,
    Choice
};

struct TtsRefreshTarget
{
    TtsRefreshTargetKind kind = TtsRefreshTargetKind::All;
    std::string id;
};

bool conversationsContainSceneId(const nlohmann::json& conversations, const std::string& sceneId)
{
    return conversations.is_object() && conversations.contains(sceneId);
}

bool scenesContainSceneId(const nlohmann::json& scenesRoot, const std::string& sceneId)
{
    const nlohmann::json& scenes = scenesRoot.value("scenes", nlohmann::json::object());
    return scenes.is_object() && scenes.contains(sceneId);
}

bool itemsContainId(const std::string& itemsPath, const std::string& itemId)
{
    if (itemsPath.empty() || itemId.empty())
        return false;
    std::ifstream file(itemsPath.c_str());
    if (!file.is_open())
        return false;
    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }
    const nlohmann::json& items =
        root.contains("items") && root["items"].is_object() ? root["items"] : root;
    return items.is_object() && items.contains(itemId);
}

bool combinationsContainId(const std::string& combinationsPath, const std::string& recipeId)
{
    if (combinationsPath.empty() || recipeId.empty())
        return false;
    std::ifstream file(combinationsPath.c_str());
    if (!file.is_open())
        return false;
    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }
    const nlohmann::json& combinations = root.value("combinations", nlohmann::json::array());
    if (!combinations.is_array())
        return false;
    for (const nlohmann::json& recipe : combinations)
    {
        if (recipe.is_object() && recipe.value("id", "") == recipeId)
            return true;
    }
    return false;
}

TtsRefreshTarget classifyRefreshTarget(
    const std::string& refreshFilter,
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& itemsPath = "",
    const std::string& combinationsPath = "")
{
    TtsRefreshTarget target;
    if (refreshFilter.empty())
        return target;

    target.id = refreshFilter;

    nlohmann::json conversations;
    {
        std::ifstream file(conversationsPath.c_str());
        if (file.is_open())
        {
            try
            {
                file >> conversations;
            }
            catch (const nlohmann::json::exception&)
            {
                conversations = nlohmann::json::object();
            }
        }
    }

    if (conversationsContainSceneId(conversations, refreshFilter))
    {
        target.kind = TtsRefreshTargetKind::Scene;
        return target;
    }

    nlohmann::json scenesRoot;
    if (!scenesPath.empty())
    {
        std::ifstream file(scenesPath.c_str());
        if (file.is_open())
        {
            try
            {
                file >> scenesRoot;
            }
            catch (const nlohmann::json::exception&)
            {
                scenesRoot = nlohmann::json::object();
            }
        }
    }

    if (scenesContainSceneId(scenesRoot, refreshFilter))
    {
        target.kind = TtsRefreshTargetKind::Scene;
        return target;
    }

    if (conversations.is_object())
    {
        for (auto sceneIt = conversations.begin(); sceneIt != conversations.end(); ++sceneIt)
        {
            if (!sceneIt.value().is_object())
                continue;

            const nlohmann::json& phases = sceneIt.value().value("speakPhases", nlohmann::json::array());
            if (!phases.is_array())
                continue;

            for (const nlohmann::json& phase : phases)
            {
                if (phase.value("id", "") == refreshFilter)
                {
                    target.kind = TtsRefreshTargetKind::Phase;
                    return target;
                }

                const nlohmann::json& lines = phase.value("lines", nlohmann::json::array());
                if (lines.is_array())
                {
                    for (const nlohmann::json& line : lines)
                    {
                        if (line.value("id", "") == refreshFilter)
                        {
                            target.kind = TtsRefreshTargetKind::RandomLine;
                            return target;
                        }
                    }
                }

                std::function<bool(const nlohmann::json&)> choiceTreeContainsId;
                choiceTreeContainsId = [&](const nlohmann::json& choices) -> bool
                {
                    if (!choices.is_array())
                        return false;

                    for (const nlohmann::json& choice : choices)
                    {
                        if (choice.value("id", "") == refreshFilter)
                            return true;
                        if (choice.contains("choices") && choiceTreeContainsId(choice["choices"]))
                            return true;
                    }

                    return false;
                };

                if (choiceTreeContainsId(phase.value("choices", nlohmann::json::array())))
                {
                    target.kind = TtsRefreshTargetKind::Choice;
                    return target;
                }
            }
        }
    }

    // Item and combination recipe ids (treated like a scene-scoped filter for collect).
    if (itemsContainId(itemsPath, refreshFilter)
        || combinationsContainId(combinationsPath, refreshFilter))
    {
        target.kind = TtsRefreshTargetKind::Scene;
        return target;
    }

    target.kind = TtsRefreshTargetKind::All;
    target.id.clear();
    return target;
}

TtsOwnerPolicy readJsonOwnerPolicy(const nlohmann::json& node)
{
    TtsOwnerPolicy policy;
    if (!node.is_object())
        return policy;
    parseTtsOwnerPolicyFromJsonFields(
        node.value("ttsEnabled", false),
        node.value("ttsDefaultVoice", ""),
        policy);
    return policy;
}

std::map<std::string, TtsOwnerPolicy> loadSceneTtsPolicies(const std::string& scenesPath)
{
    std::map<std::string, TtsOwnerPolicy> policies;
    std::ifstream file(scenesPath.c_str());
    if (!file.is_open())
        return policies;

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::exception&)
    {
        return policies;
    }

    const nlohmann::json& scenes = config.value("scenes", nlohmann::json::object());
    if (!scenes.is_object())
        return policies;

    for (auto sceneIt = scenes.begin(); sceneIt != scenes.end(); ++sceneIt)
    {
        if (sceneIt.value().is_object())
            policies[sceneIt.key()] = readJsonOwnerPolicy(sceneIt.value());
    }
    return policies;
}

void collectSceneInteractionEntries(
    std::vector<TtsVoiceEntry>& entries,
    const std::string& scenesPath,
    const std::map<std::string, TtsOwnerPolicy>& scenePolicies,
    const std::string& sceneIdFilter = "")
{
    std::ifstream file(scenesPath.c_str());
    if (!file.is_open())
        return;

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::exception&)
    {
        return;
    }

    const nlohmann::json& scenes = config.value("scenes", nlohmann::json::object());
    if (!scenes.is_object())
        return;

    for (auto sceneIt = scenes.begin(); sceneIt != scenes.end(); ++sceneIt)
    {
        if (!sceneIt.value().is_object())
            continue;

        if (!sceneIdFilter.empty() && sceneIt.key() != sceneIdFilter)
            continue;

        auto policyIt = scenePolicies.find(sceneIt.key());
        if (policyIt == scenePolicies.end() || !policyIt->second.enabled)
            continue;
        if (policyIt->second.defaultVoice.empty()
            || !isKnownBuiltinVoiceId(policyIt->second.defaultVoice))
            continue;

        const std::string& defaultVoiceId = policyIt->second.defaultVoice;

        collectSceneNarrativeEntries(entries, sceneIt.value(), defaultVoiceId);

        const nlohmann::json& subScenes = sceneIt.value().value("subScenes", nlohmann::json::object());
        if (subScenes.is_object())
        {
            for (auto subSceneIt = subScenes.begin(); subSceneIt != subScenes.end(); ++subSceneIt)
                collectSceneNarrativeEntries(entries, subSceneIt.value(), defaultVoiceId);
        }

        const nlohmann::json& interactions = sceneIt.value().value("interactions", nlohmann::json::array());
        if (!interactions.is_array())
            continue;

        for (const nlohmann::json& interaction : interactions)
        {
            addPrimaryTtsEntry(
                entries,
                interaction,
                defaultVoiceId,
                interaction.value("useDetails", ""));
            addAfterTtsEntry(entries, interaction, defaultVoiceId);

            if (interaction.value("tts", false) && !interaction.value("ttsVariantAudio", "").empty())
            {
                nlohmann::json variantNode = nlohmann::json::object();
                variantNode["tts"] = true;
                variantNode["ttsAudio"] = interaction.value("ttsVariantAudio", "");
                variantNode["ttsText"] = interaction.value("ttsVariantText", "");
                variantNode["ttsVoice"] = interaction.value("ttsVoice", defaultVoiceId);
                addEntryIfPresent(
                    entries,
                    variantNode,
                    defaultVoiceId,
                    interaction.value("ttsVariantText", ""),
                    "ttsAudio",
                    "ttsText",
                    "ttsVoice",
                    true,
                    "tts",
                    &interaction);
            }
        }
    }
}

/** Default bundled path when the editor enabled TTS without assigning ttsAudio yet. */
std::string defaultItemTtsAudioPath(const std::string& itemId, const char* bagKey)
{
    std::string leaf = "examine";
    if (std::strcmp(bagKey, "useTts") == 0)
        leaf = "use";
    else if (std::strcmp(bagKey, "takeTts") == 0)
        leaf = "take";
    else if (std::strcmp(bagKey, "examineTts") == 0)
        leaf = "examine";
    return "resources/audio/tts/items/" + itemId + "/" + leaf + ".mp3";
}

std::string defaultCombinationTtsAudioPath(const std::string& recipeId)
{
    return "resources/audio/tts/combinations/" + recipeId + "/narrative.mp3";
}

bool bagHasSpeakableTts(const nlohmann::json& bag)
{
    if (!bag.is_object())
        return false;
    if (!bag.value("tts", false) && !bag.value("enabled", false))
        return false;
    const std::string text = bag.value("ttsText", bag.value("text", ""));
    return !text.empty();
}

/** Assign ttsAudio when missing so new editor-enabled bags can be synthesized. */
bool ensureBagTtsAudioPath(nlohmann::json& bag, const std::string& defaultPath)
{
    if (!bag.is_object())
        return false;
    std::string audio = bag.value("ttsAudio", "");
    if (audio.empty())
        audio = bag.value("audio", "");
    if (!audio.empty())
        return false;
    bag["ttsAudio"] = defaultPath;
    return true;
}

void collectItemTtsEntries(
    std::vector<TtsVoiceEntry>& entries,
    const std::string& itemsPath,
    const std::string& itemIdFilter = "")
{
    if (itemsPath.empty())
        return;

    std::ifstream file(itemsPath.c_str());
    if (!file.is_open())
        return;

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return;
    }

    nlohmann::json* itemsPtr = nullptr;
    if (root.contains("items") && root["items"].is_object())
        itemsPtr = &root["items"];
    else if (root.is_object())
        itemsPtr = &root;
    if (itemsPtr == nullptr)
        return;

    nlohmann::json& items = *itemsPtr;
    bool pathsAssigned = false;

    for (auto it = items.begin(); it != items.end(); ++it)
    {
        if (!it.value().is_object())
            continue;
        if (!itemIdFilter.empty() && it.key() != itemIdFilter)
            continue;

        const TtsOwnerPolicy policy = readJsonOwnerPolicy(it.value());
        if (!policy.enabled || !isKnownBuiltinVoiceId(policy.defaultVoice))
            continue;

        const auto collectBag = [&](const char* key, const std::string& fallbackText)
        {
            if (!it.value().contains(key) || !it.value()[key].is_object())
                return;
            nlohmann::json& bag = it.value()[key];
            if (!bagHasSpeakableTts(bag) && flattenNarrationText(fallbackText).empty())
                return;
            // Enable bag if text exists under an enabled item (editor may leave tts false).
            if (!bag.value("tts", false) && !bag.value("enabled", false))
            {
                const std::string text = bag.value("ttsText", bag.value("text", ""));
                if (text.empty() && flattenNarrationText(fallbackText).empty())
                    return;
                bag["tts"] = true;
                pathsAssigned = true;
            }
            if (ensureBagTtsAudioPath(bag, defaultItemTtsAudioPath(it.key(), key)))
                pathsAssigned = true;
            if (bag.value("ttsVoice", "").empty() && bag.value("voice", "").empty())
            {
                bag["ttsVoice"] = policy.defaultVoice;
                pathsAssigned = true;
            }
            addPrimaryTtsEntry(entries, bag, policy.defaultVoice, fallbackText);
        };

        collectBag("examineTts", it.value().value("description", ""));
        collectBag("useTts", it.value().value("useNarrative", ""));
        collectBag("takeTts", "");
    }

    if (pathsAssigned)
        writeJsonFileIfChanged(itemsPath, root, true);
}

void collectCombinationTtsEntries(
    std::vector<TtsVoiceEntry>& entries,
    const std::string& combinationsPath,
    const std::string& recipeIdFilter = "")
{
    if (combinationsPath.empty())
        return;

    std::ifstream file(combinationsPath.c_str());
    if (!file.is_open())
        return;

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return;
    }

    if (!root.contains("combinations") || !root["combinations"].is_array())
        return;

    nlohmann::json& combinations = root["combinations"];
    bool pathsAssigned = false;

    for (nlohmann::json& recipe : combinations)
    {
        if (!recipe.is_object())
            continue;
        const std::string id = recipe.value("id", "");
        if (!recipeIdFilter.empty() && id != recipeIdFilter)
            continue;

        const TtsOwnerPolicy policy = readJsonOwnerPolicy(recipe);
        if (!policy.enabled || !isKnownBuiltinVoiceId(policy.defaultVoice))
            continue;

        if (!recipe.contains("narrativeTts") || !recipe["narrativeTts"].is_object())
            continue;

        nlohmann::json& bag = recipe["narrativeTts"];
        if (!bagHasSpeakableTts(bag)
            && flattenNarrationText(recipe.value("narrative", "")).empty())
            continue;
        if (!bag.value("tts", false) && !bag.value("enabled", false))
        {
            bag["tts"] = true;
            pathsAssigned = true;
        }
        if (ensureBagTtsAudioPath(bag, defaultCombinationTtsAudioPath(id.empty() ? "recipe" : id)))
            pathsAssigned = true;
        if (bag.value("ttsVoice", "").empty() && bag.value("voice", "").empty())
        {
            bag["ttsVoice"] = policy.defaultVoice;
            pathsAssigned = true;
        }
        addPrimaryTtsEntry(
            entries,
            bag,
            policy.defaultVoice,
            recipe.value("narrative", ""));
    }

    if (pathsAssigned)
        writeJsonFileIfChanged(combinationsPath, root, true);
}

}

void printGameHelp(const char* executableName)
{
    const char* programName = (executableName != nullptr && executableName[0] != '\0')
        ? executableName
        : "Highline Ridge";

    std::cout
        << "Highline Ridge  (Timberline engine)\n\n"
        << "Usage:\n"
        << "  \"" << programName << "\" [options]\n\n"
        << "Options:\n"
        << "  -h, --help                 Show this help message\n";
#if !defined(HIGHLINE_RELEASE)
    std::cout
        << "  --key=API_KEY              x.ai API key for TTS refresh commands.\n"
        << "                             The key is not stored.\n"
        << "  --refresh-voices           After editing dialog in conversations.json,\n"
        << "                             scenes.json, or items.json, regenerate bundled\n"
        << "                             voice files for every TTS line\n"
        << "                             on owners with ttsEnabled=true. Requires a valid\n"
        << "                             ttsDefaultVoice on each enabled owner (no silent\n"
        << "                             Leo default). Calls x.ai, writes\n"
        << "                             resources/audio/tts/*.mp3.xz, updates text\n"
        << "                             hashes, and exits. Requires --key. Lines whose\n"
        << "                             stored text hash matches the current dialog are\n"
        << "                             skipped. Use {{voice:eve}}...{{/voice}} in ttsText\n"
        << "                             to switch voices mid-line (ara, eve, leo, rex, sal).\n"
        << "                             Multi-voice lines save ttsAudioSegments and play\n"
        << "                             each segment in order.\n"
        << "                             Dev/authoring only — not in release builds.\n"
        << "                             Rebuild release after refreshing so the pak updates.\n"
        << "  --refresh=ID               Same as --refresh-voices, but only for one\n"
        << "                             conversation phase id, random line id, dialog\n"
        << "                             choice id, scene id, item id, or combine recipe id.\n"
        << "                             Requires --key.\n"
        << "  -force, --force            With refresh commands, ignore stored text\n"
        << "                             hashes and regenerate every matching line.\n\n";
#endif
#if defined(HIGHLINE_DEV_TOOLS)
    std::cout
        << "In-game developer tools (this build):\n"
        << "  Ctrl+Shift+S               Toggle scene debug overlay\n"
        << "  ` or ~                     Toggle developer command console\n"
        << "    give-item <item id>      Add an item (stacks if stackable)\n\n";
#endif
#if !defined(HIGHLINE_RELEASE)
    std::cout
        << "Examples:\n"
        << "  \"" << programName << "\" --key=YOUR_XAI_API_KEY --refresh-voices\n"
        << "  \"" << programName << "\" --key=YOUR_XAI_API_KEY --refresh=blackjack_invite\n"
        << "  \"" << programName << "\" --key=YOUR_XAI_API_KEY --refresh=saloon_interior\n\n"
        << "Normal play uses the bundled voice files already in resources/audio/tts/\n"
        << "and does not call x.ai or require an API key.\n";
#else
    std::cout
        << "This is a release build. TTS refresh CLI options are omitted;\n"
        << "refresh voices with a dev build, then rebuild release to repack assets.\n";
#endif
}

std::vector<TtsVoiceEntry> XaiTtsClient::collectVoiceEntries(
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& itemsPath,
    const std::string& combinationsPath,
    const std::string& refreshFilter)
{
    std::vector<TtsVoiceEntry> entries;
    const TtsRefreshTarget target = classifyRefreshTarget(
        refreshFilter,
        conversationsPath,
        scenesPath,
        itemsPath,
        combinationsPath);
    const std::map<std::string, TtsOwnerPolicy> scenePolicies = loadSceneTtsPolicies(scenesPath);

    std::ifstream file(conversationsPath.c_str());
    if (file.is_open())
    {
        nlohmann::json conversations;
        try
        {
            file >> conversations;
        }
        catch (const nlohmann::json::exception&)
        {
            conversations = nlohmann::json::object();
        }

        if (conversations.is_object())
        {
            for (auto sceneIt = conversations.begin(); sceneIt != conversations.end(); ++sceneIt)
            {
                if (!sceneIt.value().is_object())
                    continue;

                if (target.kind == TtsRefreshTargetKind::Scene && sceneIt.key() != target.id)
                    continue;

                auto policyIt = scenePolicies.find(sceneIt.key());
                if (policyIt == scenePolicies.end() || !policyIt->second.enabled)
                    continue;
                if (policyIt->second.defaultVoice.empty()
                    || !isKnownBuiltinVoiceId(policyIt->second.defaultVoice))
                    continue;

                const std::string& defaultVoiceId = policyIt->second.defaultVoice;

                const nlohmann::json& phases = sceneIt.value().value("speakPhases", nlohmann::json::array());
                if (!phases.is_array())
                    continue;

                for (const nlohmann::json& phase : phases)
                {
                    if (target.kind == TtsRefreshTargetKind::Phase
                        && phase.value("id", "") != target.id)
                        continue;

                    if (target.kind == TtsRefreshTargetKind::RandomLine)
                    {
                        collectRandomLineEntries(
                            entries,
                            phase.value("lines", nlohmann::json::array()),
                            defaultVoiceId,
                            target.id);
                        continue;
                    }

                    if (target.kind == TtsRefreshTargetKind::Choice)
                    {
                        collectChoiceEntries(
                            entries,
                            phase.value("choices", nlohmann::json::array()),
                            defaultVoiceId,
                            target.id);
                        continue;
                    }

                    addPrimaryTtsEntry(
                        entries,
                        phase,
                        defaultVoiceId,
                        phase.value("intro", phase.value("text", "")));
                    addAfterTtsEntry(entries, phase, defaultVoiceId);

                    if (phase.value("resumeTts", false))
                    {
                        nlohmann::json resumeNode = nlohmann::json::object();
                        resumeNode["tts"] = true;
                        resumeNode["ttsAudio"] = phase.value("resumeTtsAudio", "");
                        resumeNode["ttsText"] = phase.value("resumeTtsText", "");
                        resumeNode["ttsVoice"] = phase.value("resumeTtsVoice", defaultVoiceId);
                        addPrimaryTtsEntry(
                            entries,
                            resumeNode,
                            defaultVoiceId,
                            phase.value("resumeIntro", ""),
                            &phase);
                    }

                    collectChoiceEntries(entries, phase.value("choices", nlohmann::json::array()), defaultVoiceId);
                    collectRandomLineEntries(entries, phase.value("lines", nlohmann::json::array()), defaultVoiceId);
                }
            }
        }
    }

    if (!scenesPath.empty()
        && (target.kind == TtsRefreshTargetKind::All || target.kind == TtsRefreshTargetKind::Scene))
    {
        const std::string sceneIdFilter =
            target.kind == TtsRefreshTargetKind::Scene ? target.id : "";
        collectSceneInteractionEntries(entries, scenesPath, scenePolicies, sceneIdFilter);
    }

    if (target.kind == TtsRefreshTargetKind::All)
    {
        collectItemTtsEntries(entries, itemsPath);
        collectCombinationTtsEntries(entries, combinationsPath);
    }
    else if (target.kind == TtsRefreshTargetKind::Scene)
    {
        // Item / recipe ids can also be passed as --refresh=ID
        collectItemTtsEntries(entries, itemsPath, target.id);
        collectCombinationTtsEntries(entries, combinationsPath, target.id);
    }

    return entries;
}

bool isLikelyTtsApiErrorPayload(const std::vector<unsigned char>& bytes)
{
    if (bytes.empty())
        return true;

    if (bytes[0] != '{')
        return false;

    const std::string payload(bytes.begin(), bytes.end());
    return payload.find("\"error\"") != std::string::npos
        || payload.find("\"code\"") != std::string::npos;
}

bool XaiTtsClient::synthesizeToFile(
    const std::string& apiKey,
    const std::string& text,
    const std::string& voiceId,
    const std::string& outputPath)
{
    if (apiKey.empty() || text.empty() || outputPath.empty())
        return false;

    if (!ensureParentDirectoryExists(outputPath))
        return false;

    nlohmann::json payload;
    payload["text"] = text;
    payload["voice_id"] = normalizeVoiceId(voiceId);
    payload["language"] = "en";

    const std::string payloadPath = outputPath + ".json";
    {
        std::ofstream payloadFile(payloadPath.c_str());
        if (!payloadFile.is_open())
            return false;
        payloadFile << payload.dump();
        if (!payloadFile.good())
            return false;
    }

    const std::string httpCodePath = outputPath + ".http";
    std::ostringstream command;
    command << curlExecutable() << " -sS -X POST https://api.x.ai/v1/tts "
            << "-H \"Authorization: Bearer " << apiKey << "\" "
            << "-H \"Content-Type: application/json\" "
            << "-d @\"" << payloadPath << "\" "
            << "-o \"" << outputPath << "\" "
            << "-w \"%{http_code}\" > \"" << httpCodePath << "\"";
    const int exitCode = std::system(command.str().c_str());
    std::remove(payloadPath.c_str());

    std::string httpCode;
    {
        std::ifstream httpCodeFile(httpCodePath.c_str());
        if (httpCodeFile.is_open())
            std::getline(httpCodeFile, httpCode);
    }
    std::remove(httpCodePath.c_str());

    if (exitCode != 0 || !FileExists(outputPath.c_str()))
    {
        std::remove(outputPath.c_str());
        std::cerr << "TTS request failed for voice '" << voiceId << "' (curl exit " << exitCode
                  << ")\n";
        return false;
    }

    std::vector<unsigned char> responseBytes;
    if (!loadAssetBytesFromFile(outputPath, responseBytes) || responseBytes.empty())
    {
        std::remove(outputPath.c_str());
        std::cerr << "TTS response was empty for voice '" << voiceId << "'\n";
        return false;
    }

    if (httpCode != "200" || isLikelyTtsApiErrorPayload(responseBytes))
    {
        const std::string responseText(responseBytes.begin(), responseBytes.end());
        std::cerr << "TTS API error";
        if (!httpCode.empty())
            std::cerr << " (" << httpCode << ")";
        std::cerr << " for voice '" << voiceId << "': " << responseText << "\n";
        std::remove(outputPath.c_str());
        return false;
    }

    return true;
}

VoiceBundleResult XaiTtsClient::bundleVoiceFile(
    const std::string& apiKey,
    const TtsVoiceEntry& entry,
    const std::string& assetRoot,
    bool forceRefresh)
{
    VoiceBundleResult result;
    result.textSha256 = sha256Hex(entry.text);

    const std::vector<std::string> audioPaths = buildAssetSearchPaths(assetRoot, entry.audioPath);
    std::string resolvedAudioPath;
    for (const std::string& path : audioPaths)
    {
        if (FileExists(path.c_str())
            || FileExists(compressedAssetPath(path).c_str()))
        {
            resolvedAudioPath = path;
            break;
        }
    }
    if (resolvedAudioPath.empty())
        resolvedAudioPath = resolveAssetPath(assetRoot, entry.audioPath);

    std::vector<TtsVoiceSegment> voiceSegments;
    std::string markupError;
    if (!parseVoiceMarkup(entry.text, entry.voiceId, voiceSegments, markupError))
    {
        std::cerr << "Invalid ttsText for " << entry.audioPath << ": " << markupError << "\n";
        return result;
    }

    const std::vector<std::string> segmentAudioPaths =
        buildSegmentAudioPaths(entry.audioPath, voiceSegments.size());
    const bool bundleExists =
        allSegmentBundlesExist(assetRoot, entry.audioPath, segmentAudioPaths);
    const bool hashMatchesStored = !entry.storedTextSha256.empty()
        && entry.storedTextSha256 == result.textSha256;

    if (!forceRefresh)
    {
        if (hashMatchesStored && bundleExists)
        {
            TraceLog(LOG_INFO, "TTS text unchanged, skipping: %s", entry.audioPath.c_str());
            result.success = true;
            result.skipped = true;
            return result;
        }

        if (hashMatchesStored && !bundleExists)
            TraceLog(LOG_INFO, "TTS text unchanged, bundle missing, refreshing: %s",
                     entry.audioPath.c_str());
        else if (!bundleExists)
            TraceLog(LOG_INFO, "TTS bundle missing, refreshing: %s", entry.audioPath.c_str());
        else if (entry.storedTextSha256.empty())
            TraceLog(LOG_INFO, "TTS missing stored hash, refreshing: %s", entry.audioPath.c_str());
        else
            TraceLog(LOG_INFO, "TTS text changed, refreshing: %s", entry.audioPath.c_str());
    }
    else
    {
        TraceLog(LOG_INFO, "TTS force refresh: %s", entry.audioPath.c_str());
    }

    removeStaleTtsBundles(assetRoot, entry.audioPath);

    for (size_t segmentIndex = 0; segmentIndex < voiceSegments.size(); ++segmentIndex)
    {
        const TtsVoiceSegment& segment = voiceSegments[segmentIndex];
        const std::string& segmentAudioPath = segmentAudioPaths[segmentIndex];
        const std::vector<std::string> segmentSearchPaths =
            buildAssetSearchPaths(assetRoot, segmentAudioPath);
        std::string resolvedSegmentPath;
        for (const std::string& path : segmentSearchPaths)
        {
            resolvedSegmentPath = path;
            break;
        }
        if (resolvedSegmentPath.empty())
            resolvedSegmentPath = resolveAssetPath(assetRoot, segmentAudioPath);

        const std::string bundledSegmentXzPath = compressedAssetPath(resolvedSegmentPath);
        const std::string tempMp3Path = resolvedSegmentPath + ".tmp.mp3";
        std::remove(tempMp3Path.c_str());

        TraceLog(
            LOG_INFO,
            "TTS segment %zu/%zu voice=%s path=%s",
            segmentIndex + 1,
            voiceSegments.size(),
            segment.voiceId.c_str(),
            bundledSegmentXzPath.c_str());

        if (!synthesizeToFile(apiKey, segment.text, segment.voiceId, tempMp3Path))
            return result;

        std::vector<unsigned char> segmentBytes;
        if (!loadAssetBytesFromFile(tempMp3Path, segmentBytes) || segmentBytes.empty())
        {
            std::remove(tempMp3Path.c_str());
            return result;
        }

        if (!compressBytesToXzFile(
                segmentBytes.data(),
                segmentBytes.size(),
                bundledSegmentXzPath))
        {
            std::remove(tempMp3Path.c_str());
            return result;
        }

        std::remove(tempMp3Path.c_str());
        std::remove(resolvedSegmentPath.c_str());
        if (!mirrorBundleToSourceTree(bundledSegmentXzPath))
        {
            TraceLog(LOG_WARNING, "Saved runtime TTS bundle but failed to mirror: %s",
                     bundledSegmentXzPath.c_str());
        }
    }

    TraceLog(
        LOG_INFO,
        "Saved %zu TTS segment bundle(s) for %s",
        voiceSegments.size(),
        entry.audioPath.c_str());
    result.success = true;
    result.regenerated = true;
    result.segmentAudioPaths = segmentAudioPaths;
    return result;
}

bool persistSha256InResourceFile(
    const std::string& path,
    const std::string& audioPath,
    const std::string& textSha256)
{
    if (path.empty())
        return false;
    std::ifstream in(path.c_str());
    if (!in.is_open())
        return false;
    nlohmann::json root;
    try
    {
        in >> root;
        if (updateSha256InJsonTree(root, audioPath, textSha256)
            && writeJsonFileIfChanged(path, root, true))
            return true;
    }
    catch (const nlohmann::json::exception&)
    {
    }
    return false;
}

bool persistSegmentsInResourceFile(
    const std::string& path,
    const std::string& audioPath,
    const std::vector<std::string>& segmentPaths)
{
    if (path.empty())
        return false;
    std::ifstream in(path.c_str());
    if (!in.is_open())
        return false;
    nlohmann::json root;
    try
    {
        in >> root;
        if (updateAudioSegmentsInJsonTree(root, audioPath, segmentPaths)
            && writeJsonFileIfChanged(path, root, true))
            return true;
    }
    catch (const nlohmann::json::exception&)
    {
    }
    return false;
}

bool XaiTtsClient::persistVoiceTextSha256(
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& itemsPath,
    const std::string& combinationsPath,
    const std::string& audioPath,
    const std::string& textSha256)
{
    bool persisted = false;
    persisted = persistSha256InResourceFile(conversationsPath, audioPath, textSha256) || persisted;
    persisted = persistSha256InResourceFile(scenesPath, audioPath, textSha256) || persisted;
    persisted = persistSha256InResourceFile(itemsPath, audioPath, textSha256) || persisted;
    persisted = persistSha256InResourceFile(combinationsPath, audioPath, textSha256) || persisted;
    return persisted;
}

bool XaiTtsClient::persistVoiceAudioSegments(
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& itemsPath,
    const std::string& combinationsPath,
    const std::string& audioPath,
    const std::vector<std::string>& segmentPaths)
{
    bool persisted = false;
    persisted =
        persistSegmentsInResourceFile(conversationsPath, audioPath, segmentPaths) || persisted;
    persisted = persistSegmentsInResourceFile(scenesPath, audioPath, segmentPaths) || persisted;
    persisted = persistSegmentsInResourceFile(itemsPath, audioPath, segmentPaths) || persisted;
    persisted =
        persistSegmentsInResourceFile(combinationsPath, audioPath, segmentPaths) || persisted;
    return persisted;
}

int XaiTtsClient::refreshBundledVoices(
    const std::string& apiKey,
    const std::string& assetRoot,
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& itemsPath,
    const std::string& combinationsPath,
    bool forceRefresh,
    const std::string& refreshFilter)
{
    const std::string trimmedKey = trimWhitespace(apiKey);
    if (trimmedKey.empty())
    {
        std::cerr << "Missing API key. Use --key=YOUR_XAI_API_KEY\n";
        return 1;
    }

    if (!validateTtsResourcesOrLog(scenesPath, conversationsPath, itemsPath, combinationsPath))
    {
        std::cerr << "TTS validation failed; refusing to refresh voices.\n";
        return 1;
    }

    const std::string trimmedFilter = trimWhitespace(refreshFilter);
    if (!trimmedFilter.empty())
    {
        const TtsRefreshTarget target = classifyRefreshTarget(
            trimmedFilter,
            conversationsPath,
            scenesPath,
            itemsPath,
            combinationsPath);
        if (target.id.empty())
        {
            std::cerr << "Unknown refresh id: " << trimmedFilter << "\n";
            return 1;
        }
    }

    const std::vector<TtsVoiceEntry> entries =
        collectVoiceEntries(
            conversationsPath,
            scenesPath,
            itemsPath,
            combinationsPath,
            trimmedFilter);
    if (entries.empty())
    {
        if (!trimmedFilter.empty())
            std::cerr << "No TTS entries found for refresh id: " << trimmedFilter << "\n";
        else
            std::cerr << "No TTS entries found (check ttsEnabled owners and ttsText/ttsAudio bags)\n";
        return 1;
    }

    int successCount = 0;
    int skippedCount = 0;
    int regeneratedCount = 0;
    for (const TtsVoiceEntry& entry : entries)
    {
        const VoiceBundleResult bundleResult =
            bundleVoiceFile(trimmedKey, entry, assetRoot, forceRefresh);
        if (!bundleResult.success)
        {
            std::cerr << "Failed to refresh: " << entry.audioPath << "\n";
            continue;
        }

        ++successCount;
        if (bundleResult.skipped)
        {
            ++skippedCount;
            continue;
        }

        ++regeneratedCount;
        bool persisted = persistVoiceTextSha256(
            conversationsPath,
            scenesPath,
            itemsPath,
            combinationsPath,
            entry.audioPath,
            bundleResult.textSha256);

        const std::string runtimeConversationsPath =
            runtimeResourcePath(conversationsPath);
        const std::string runtimeScenesPath = runtimeResourcePath(scenesPath);
        const std::string runtimeItemsPath = runtimeResourcePath(itemsPath);
        const std::string runtimeCombinationsPath = runtimeResourcePath(combinationsPath);
        if (runtimeConversationsPath != conversationsPath
            || runtimeScenesPath != scenesPath
            || runtimeItemsPath != itemsPath
            || runtimeCombinationsPath != combinationsPath)
        {
            persisted = persistVoiceTextSha256(
                        runtimeConversationsPath,
                        runtimeScenesPath,
                        runtimeItemsPath,
                        runtimeCombinationsPath,
                        entry.audioPath,
                        bundleResult.textSha256)
                || persisted;
        }

        if (!persisted)
        {
            std::cerr << "Warning: refreshed audio but failed to persist text hash for "
                      << entry.audioPath << "\n";
        }

        bool segmentsPersisted = persistVoiceAudioSegments(
            conversationsPath,
            scenesPath,
            itemsPath,
            combinationsPath,
            entry.audioPath,
            bundleResult.segmentAudioPaths);
        if (runtimeConversationsPath != conversationsPath
            || runtimeScenesPath != scenesPath
            || runtimeItemsPath != itemsPath
            || runtimeCombinationsPath != combinationsPath)
        {
            segmentsPersisted = persistVoiceAudioSegments(
                                    runtimeConversationsPath,
                                    runtimeScenesPath,
                                    runtimeItemsPath,
                                    runtimeCombinationsPath,
                                    entry.audioPath,
                                    bundleResult.segmentAudioPaths)
                || segmentsPersisted;
        }

        if (!segmentsPersisted && bundleResult.segmentAudioPaths.size() > 1)
        {
            std::cerr << "Warning: refreshed audio but failed to persist segment paths for "
                      << entry.audioPath << "\n";
        }
    }

    std::cout << "Refreshed " << regeneratedCount << " voice line(s), skipped "
              << skippedCount << " unchanged, " << successCount << " / "
              << entries.size() << " total into resources/audio/tts/*.mp3.xz\n";
    return successCount == static_cast<int>(entries.size()) ? 0 : 2;
}

}