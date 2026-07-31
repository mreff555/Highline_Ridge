/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#include "TtsContentValidator.h"

#include "TtsVoiceMarkup.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <raylib.h>

namespace timberline_engine
{

namespace
{

bool loadJsonFile(const std::string& path, nlohmann::json& out)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;
    try
    {
        file >> out;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }
    return true;
}

TtsOwnerPolicy readOwnerPolicy(const nlohmann::json& node)
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

void addIssue(std::vector<TtsValidationIssue>& issues, const std::string& message)
{
    issues.push_back({ message });
}

void validatePolicy(
    const std::string& ownerLabel,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!policy.enabled)
        return;
    if (policy.defaultVoice.empty() || !isKnownBuiltinVoiceId(policy.defaultVoice))
        addIssue(issues, formatTtsEnabledMissingVoiceError(ownerLabel));
}

bool nodeHasTtsFlag(const nlohmann::json& node, const char* flagField = "tts")
{
    if (!node.is_object())
        return false;
    if (node.value(flagField, false))
        return true;
    // Narrative/item bags also accept "enabled"
    if (std::string(flagField) == "tts" && node.value("enabled", false))
        return true;
    return false;
}

std::string readTtsText(const nlohmann::json& node, const char* textField = "ttsText")
{
    if (!node.is_object())
        return "";
    std::string text = node.value(textField, "");
    if (text.empty() && std::string(textField) == "ttsText")
        text = node.value("text", "");
    return text;
}

std::string readLineVoice(
    const nlohmann::json& node,
    const std::string& ownerDefault,
    const char* voiceField = "ttsVoice")
{
    if (!node.is_object())
        return ownerDefault;
    std::string voice = node.value(voiceField, "");
    if (voice.empty())
        voice = node.value("voice", "");
    if (voice.empty())
        return ownerDefault;
    return normalizeVoiceId(voice);
}

void validateMarkupField(
    const std::string& pathLabel,
    const nlohmann::json& node,
    const TtsOwnerPolicy& policy,
    const char* flagField,
    const char* textField,
    const char* voiceField,
    std::vector<TtsValidationIssue>& issues)
{
    if (!policy.enabled || !node.is_object())
        return;
    if (!nodeHasTtsFlag(node, flagField))
        return;

    const std::string text = readTtsText(node, textField);
    if (text.empty())
        return;

    const std::string voice = readLineVoice(node, policy.defaultVoice, voiceField);
    if (!isKnownBuiltinVoiceId(voice))
    {
        addIssue(
            issues,
            pathLabel + ": invalid ttsVoice \"" + voice
                + "\". Current voices available are: " + formatBuiltinVoiceList());
        return;
    }

    std::vector<TtsVoiceSegment> segments;
    std::string error;
    if (!parseVoiceMarkup(text, voice, segments, error))
        addIssue(issues, pathLabel + ": " + error);
}

void validateAfterMarkup(
    const std::string& pathLabel,
    const nlohmann::json& node,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!policy.enabled || !node.is_object())
        return;
    if (!node.value("ttsAfter", false))
        return;

    const std::string text = node.value("ttsAfterText", "");
    if (text.empty())
        return;

    const std::string voice = readLineVoice(node, policy.defaultVoice, "ttsAfterVoice");
    if (!isKnownBuiltinVoiceId(voice))
    {
        addIssue(
            issues,
            pathLabel + " (ttsAfter): invalid voice \"" + voice
                + "\". Current voices available are: " + formatBuiltinVoiceList());
        return;
    }

    std::vector<TtsVoiceSegment> segments;
    std::string error;
    if (!parseVoiceMarkup(text, voice, segments, error))
        addIssue(issues, pathLabel + " (ttsAfter): " + error);
}

void validatePrimaryAndAfter(
    const std::string& pathLabel,
    const nlohmann::json& node,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    validateMarkupField(pathLabel, node, policy, "tts", "ttsText", "ttsVoice", issues);
    validateAfterMarkup(pathLabel, node, policy, issues);
}

void validateNestedTtsBag(
    const std::string& pathLabel,
    const nlohmann::json& parent,
    const char* bagKey,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!parent.is_object() || !parent.contains(bagKey) || !parent[bagKey].is_object())
        return;
    validateMarkupField(
        pathLabel + "." + bagKey,
        parent[bagKey],
        policy,
        "tts",
        "ttsText",
        "ttsVoice",
        issues);
}

void validateChoices(
    const std::string& pathLabel,
    const nlohmann::json& choices,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!choices.is_array())
        return;
    for (size_t i = 0; i < choices.size(); ++i)
    {
        const nlohmann::json& choice = choices[i];
        if (!choice.is_object())
            continue;
        const std::string id = choice.value("id", std::to_string(i));
        const std::string label = pathLabel + ".choices[" + id + "]";
        validatePrimaryAndAfter(label, choice, policy, issues);
        if (choice.contains("choices"))
            validateChoices(label, choice["choices"], policy, issues);
    }
}

void validateRandomLines(
    const std::string& pathLabel,
    const nlohmann::json& lines,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!lines.is_array())
        return;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const nlohmann::json& line = lines[i];
        if (!line.is_object())
            continue;
        const std::string id = line.value("id", std::to_string(i));
        validatePrimaryAndAfter(pathLabel + ".lines[" + id + "]", line, policy, issues);
    }
}

void validateSpeakPhases(
    const std::string& pathLabel,
    const nlohmann::json& phases,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!phases.is_array())
        return;
    for (size_t i = 0; i < phases.size(); ++i)
    {
        const nlohmann::json& phase = phases[i];
        if (!phase.is_object())
            continue;
        const std::string id = phase.value("id", std::to_string(i));
        const std::string label = pathLabel + ".speakPhases[" + id + "]";
        validatePrimaryAndAfter(label, phase, policy, issues);

        if (phase.value("resumeTts", false))
        {
            nlohmann::json resumeNode = nlohmann::json::object();
            resumeNode["tts"] = true;
            resumeNode["ttsText"] = phase.value("resumeTtsText", "");
            resumeNode["ttsVoice"] = phase.value("resumeTtsVoice", "");
            validateMarkupField(
                label + ".resumeTts",
                resumeNode,
                policy,
                "tts",
                "ttsText",
                "ttsVoice",
                issues);
        }

        validateChoices(label, phase.value("choices", nlohmann::json::array()), policy, issues);
        validateRandomLines(label, phase.value("lines", nlohmann::json::array()), policy, issues);
    }
}

void validateSceneNarrativeNode(
    const std::string& pathLabel,
    const nlohmann::json& node,
    const TtsOwnerPolicy& policy,
    std::vector<TtsValidationIssue>& issues)
{
    if (!node.is_object())
        return;

    validateNestedTtsBag(pathLabel, node, "descriptionTts", policy, issues);
    validateNestedTtsBag(pathLabel, node, "examineTts", policy, issues);
    validateNestedTtsBag(pathLabel, node, "wakeTts", policy, issues);

    const nlohmann::json& interactions = node.value("interactions", nlohmann::json::array());
    if (interactions.is_array())
    {
        for (size_t i = 0; i < interactions.size(); ++i)
        {
            const nlohmann::json& interaction = interactions[i];
            if (!interaction.is_object())
                continue;
            const std::string id = interaction.value("id", std::to_string(i));
            validatePrimaryAndAfter(
                pathLabel + ".interactions[" + id + "]",
                interaction,
                policy,
                issues);

            if (interaction.value("tts", false) && !interaction.value("ttsVariantText", "").empty())
            {
                nlohmann::json variant = nlohmann::json::object();
                variant["tts"] = true;
                variant["ttsText"] = interaction.value("ttsVariantText", "");
                variant["ttsVoice"] = interaction.value("ttsVoice", "");
                validateMarkupField(
                    pathLabel + ".interactions[" + id + "].ttsVariant",
                    variant,
                    policy,
                    "tts",
                    "ttsText",
                    "ttsVoice",
                    issues);
            }
        }
    }

    const nlohmann::json& subScenes = node.value("subScenes", nlohmann::json::object());
    if (subScenes.is_object())
    {
        for (auto it = subScenes.begin(); it != subScenes.end(); ++it)
        {
            validateSceneNarrativeNode(
                pathLabel + ".subScenes." + it.key(),
                it.value(),
                policy,
                issues);
        }
    }
}

}

bool validateTtsResources(
    const std::string& scenesPath,
    const std::string& conversationsPath,
    const std::string& itemsPath,
    const std::string& combinationsPath,
    std::vector<TtsValidationIssue>& outIssues)
{
    outIssues.clear();

    nlohmann::json scenesRoot;
    nlohmann::json conversations;
    nlohmann::json itemsRoot;
    nlohmann::json combinationsRoot;

    const bool scenesOk = !scenesPath.empty() && loadJsonFile(scenesPath, scenesRoot);
    const bool conversationsOk =
        !conversationsPath.empty() && loadJsonFile(conversationsPath, conversations);
    const bool itemsOk = !itemsPath.empty() && loadJsonFile(itemsPath, itemsRoot);
    const bool combinationsOk =
        !combinationsPath.empty() && loadJsonFile(combinationsPath, combinationsRoot);

    std::map<std::string, TtsOwnerPolicy> scenePolicies;

    if (scenesOk)
    {
        const nlohmann::json& scenes = scenesRoot.value("scenes", nlohmann::json::object());
        if (scenes.is_object())
        {
            for (auto it = scenes.begin(); it != scenes.end(); ++it)
            {
                if (!it.value().is_object())
                    continue;
                const TtsOwnerPolicy policy = readOwnerPolicy(it.value());
                scenePolicies[it.key()] = policy;
                validatePolicy(it.key(), policy, outIssues);
                if (policy.enabled)
                    validateSceneNarrativeNode(it.key(), it.value(), policy, outIssues);
            }
        }
    }

    if (conversationsOk && conversations.is_object())
    {
        for (auto it = conversations.begin(); it != conversations.end(); ++it)
        {
            if (!it.value().is_object())
                continue;

            TtsOwnerPolicy policy;
            auto found = scenePolicies.find(it.key());
            if (found != scenePolicies.end())
                policy = found->second;
            else
            {
                // Conversation without a scenes.json entry: treat as off.
                policy.enabled = false;
            }

            // Policy validity already checked for known scenes.
            if (!policy.enabled)
                continue;

            validateSpeakPhases(
                "conversation " + it.key(),
                it.value().value("speakPhases", nlohmann::json::array()),
                policy,
                outIssues);
        }
    }

    if (itemsOk)
    {
        const nlohmann::json& items = itemsRoot.contains("items") && itemsRoot["items"].is_object()
            ? itemsRoot["items"]
            : itemsRoot;
        if (items.is_object())
        {
            for (auto it = items.begin(); it != items.end(); ++it)
            {
                if (!it.value().is_object())
                    continue;
                const TtsOwnerPolicy policy = readOwnerPolicy(it.value());
                const std::string label = "item " + it.key();
                validatePolicy(label, policy, outIssues);
                if (!policy.enabled)
                    continue;
                validateNestedTtsBag(label, it.value(), "examineTts", policy, outIssues);
                validateNestedTtsBag(label, it.value(), "useTts", policy, outIssues);
                validateNestedTtsBag(label, it.value(), "takeTts", policy, outIssues);
            }
        }
    }

    if (combinationsOk)
    {
        const nlohmann::json& combinations =
            combinationsRoot.value("combinations", nlohmann::json::array());
        if (combinations.is_array())
        {
            for (size_t i = 0; i < combinations.size(); ++i)
            {
                const nlohmann::json& recipe = combinations[i];
                if (!recipe.is_object())
                    continue;
                const std::string id = recipe.value("id", std::to_string(i));
                const std::string label = "combine recipe " + id;
                const TtsOwnerPolicy policy = readOwnerPolicy(recipe);
                validatePolicy(label, policy, outIssues);
                if (!policy.enabled)
                    continue;
                validateNestedTtsBag(label, recipe, "narrativeTts", policy, outIssues);
            }
        }
    }

    return outIssues.empty();
}

bool validateTtsResourcesOrLog(
    const std::string& scenesPath,
    const std::string& conversationsPath,
    const std::string& itemsPath,
    const std::string& combinationsPath)
{
    std::vector<TtsValidationIssue> issues;
    if (validateTtsResources(
            scenesPath,
            conversationsPath,
            itemsPath,
            combinationsPath,
            issues))
    {
        return true;
    }

    for (const TtsValidationIssue& issue : issues)
    {
        TraceLog(LOG_ERROR, "TTS validation: %s", issue.message.c_str());
        std::cerr << "TTS validation error: " << issue.message << "\n";
    }
    TraceLog(
        LOG_ERROR,
        "TTS validation failed with %d issue(s). Fix resources or disable TTS on the listed owners.",
        static_cast<int>(issues.size()));
    return false;
}

}
