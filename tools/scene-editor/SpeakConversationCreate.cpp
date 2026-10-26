/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SpeakConversationCreate.h"

#include "SceneDocument.h"

#include <cctype>

namespace timberline_editor
{
namespace
{

std::string trimCopy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

bool isValidSpeakId(const std::string& id)
{
    if (id.empty() || id.size() > 64)
        return false;
    if (!std::isalpha(static_cast<unsigned char>(id[0])))
        return false;
    for (char ch : id)
    {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (!(std::isalnum(u) || ch == '_'))
            return false;
    }
    return true;
}

} // namespace

std::string sanitizeSpeakId(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw)
    {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u))
            out.push_back(static_cast<char>(std::tolower(u)));
        else if (ch == '_' || ch == '-' || ch == ' ' || ch == '/' || ch == '.')
        {
            if (!out.empty() && out.back() != '_')
                out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '_')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    // Collapse repeats.
    std::string compact;
    for (char ch : out)
    {
        if (ch == '_' && !compact.empty() && compact.back() == '_')
            continue;
        compact.push_back(ch);
    }
    return compact;
}

std::string formatSpeakActorDisplayName(const std::string& actorId)
{
    if (actorId.empty())
        return "";
    std::string formatted;
    formatted.reserve(actorId.size());
    bool capitalizeNext = true;
    for (char character : actorId)
    {
        if (character == '_')
        {
            formatted.push_back(' ');
            capitalizeNext = true;
            continue;
        }
        if (capitalizeNext)
        {
            formatted.push_back(
                static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
            capitalizeNext = false;
        }
        else
            formatted.push_back(character);
    }
    return formatted;
}

void fillSpeakConversationCreateDefaults(
    SpeakConversationCreatePayload& payload,
    const std::string& sceneId)
{
    payload.sceneId = sceneId;
    if (payload.actorId.empty())
        payload.actorId = "npc";
    payload.actorId = sanitizeSpeakId(payload.actorId);
    if (payload.phaseId.empty())
        payload.phaseId = payload.actorId;
    else
        payload.phaseId = sanitizeSpeakId(payload.phaseId);
    if (payload.actorName.empty())
        payload.actorName = formatSpeakActorDisplayName(payload.actorId);
    if (payload.intro.empty())
        payload.intro = "(Opening line — edit me.)";
    if (payload.starterChoiceId.empty())
        payload.starterChoiceId = "player_opener";
    else
        payload.starterChoiceId = sanitizeSpeakId(payload.starterChoiceId);
    if (payload.starterChoiceLabel.empty())
        payload.starterChoiceLabel = "What brings you here?";
}

bool sceneHasConversationEntry(const DocumentWorkspace& docs, const std::string& sceneId)
{
    if (!docs.conversationsLoaded || sceneId.empty() || !docs.conversationsRoot.is_object())
        return false;
    return docs.conversationsRoot.contains(sceneId)
        && docs.conversationsRoot[sceneId].is_object();
}

bool sceneHasSpeakPhases(const DocumentWorkspace& docs, const std::string& sceneId)
{
    if (!sceneHasConversationEntry(docs, sceneId))
        return false;
    const nlohmann::json& node = docs.conversationsRoot[sceneId];
    if (!node.contains("speakPhases") || !node["speakPhases"].is_array())
        return false;
    return !node["speakPhases"].empty();
}

SpeakConversationCreateStatus createSpeakConversationScaffold(
    DocumentWorkspace& docs,
    const SpeakConversationCreatePayload& payloadIn,
    std::string& errorOut)
{
    errorOut.clear();
    SpeakConversationCreatePayload payload = payloadIn;
    fillSpeakConversationCreateDefaults(payload, payload.sceneId);

    if (payload.sceneId.empty() || !docs.scenes.hasScene(payload.sceneId))
    {
        errorOut = "Scene \"" + payload.sceneId + "\" does not exist.";
        return SpeakConversationCreateStatus::SceneMissing;
    }
    if (!docs.conversationsLoaded)
    {
        errorOut = "Conversations document is not loaded.";
        return SpeakConversationCreateStatus::ConversationsNotLoaded;
    }
    if (sceneHasSpeakPhases(docs, payload.sceneId))
    {
        errorOut = "Scene already has speakPhases — Open existing instead.";
        return SpeakConversationCreateStatus::AlreadyHasPhases;
    }
    if (!isValidSpeakId(payload.actorId) || !isValidSpeakId(payload.phaseId)
        || !isValidSpeakId(payload.starterChoiceId))
    {
        errorOut = "Actor / phase / choice ids must start with a letter "
                   "(a-z, 0-9, underscore).";
        return SpeakConversationCreateStatus::InvalidIds;
    }
    payload.intro = trimCopy(payload.intro);
    if (payload.intro.empty())
    {
        errorOut = "Intro text is required.";
        return SpeakConversationCreateStatus::EmptyIntro;
    }
    payload.starterChoiceLabel = trimCopy(payload.starterChoiceLabel);
    if (payload.starterChoiceLabel.empty())
    {
        errorOut = "Starter choice label is required.";
        return SpeakConversationCreateStatus::InvalidChoice;
    }
    payload.actorName = trimCopy(payload.actorName);
    if (payload.actorName.empty())
        payload.actorName = formatSpeakActorDisplayName(payload.actorId);

    nlohmann::json choice = nlohmann::json::object();
    choice["id"] = payload.starterChoiceId;
    choice["label"] = payload.starterChoiceLabel;
    choice["response"] = "(Reply — edit me.)";
    choice["closePhase"] = false;

    nlohmann::json phase = nlohmann::json::object();
    phase["id"] = payload.phaseId;
    phase["type"] = "scripted";
    phase["actorId"] = payload.actorId;
    phase["actorName"] = payload.actorName;
    phase["resetOnSceneEnter"] = true;
    phase["intro"] = payload.intro;
    phase["choices"] = nlohmann::json::array({choice});

    try
    {
        if (!docs.conversationsRoot.is_object())
            docs.conversationsRoot = nlohmann::json::object();

        nlohmann::json* entry = nullptr;
        if (docs.conversationsRoot.contains(payload.sceneId)
            && docs.conversationsRoot[payload.sceneId].is_object())
        {
            entry = &docs.conversationsRoot[payload.sceneId];
        }
        else
        {
            docs.conversationsRoot[payload.sceneId] = nlohmann::json::object();
            entry = &docs.conversationsRoot[payload.sceneId];
        }
        (*entry)["speakPhases"] = nlohmann::json::array({phase});

        nlohmann::json* scene = docs.scenes.sceneJson(payload.sceneId);
        if (scene == nullptr || !scene->is_object())
        {
            errorOut = "Failed to open scene JSON for actions.speak.";
            return SpeakConversationCreateStatus::MutateFailed;
        }
        if (!scene->contains("actions") || !(*scene)["actions"].is_object())
            (*scene)["actions"] = nlohmann::json::object();
        (*scene)["actions"]["speak"] = true;

        docs.markDirty();
    }
    catch (const nlohmann::json::exception& ex)
    {
        errorOut = std::string("JSON mutate failed: ") + ex.what();
        return SpeakConversationCreateStatus::MutateFailed;
    }

    return SpeakConversationCreateStatus::Ok;
}

SpeakConversationDeleteStatus deleteSpeakConversation(
    DocumentWorkspace& docs,
    const std::string& sceneId,
    bool clearSpeakActionIfNoDetails,
    std::string& errorOut)
{
    errorOut.clear();
    if (sceneId.empty() || !docs.scenes.hasScene(sceneId))
    {
        errorOut = "Scene \"" + sceneId + "\" does not exist.";
        return SpeakConversationDeleteStatus::SceneMissing;
    }
    if (!docs.conversationsLoaded)
    {
        errorOut = "Conversations document is not loaded.";
        return SpeakConversationDeleteStatus::ConversationsNotLoaded;
    }
    if (!sceneHasConversationEntry(docs, sceneId))
    {
        errorOut = "No conversation entry for this scene.";
        return SpeakConversationDeleteStatus::NothingToDelete;
    }

    try
    {
        docs.conversationsRoot.erase(sceneId);

        if (clearSpeakActionIfNoDetails)
        {
            nlohmann::json* scene = docs.scenes.sceneJson(sceneId);
            if (scene != nullptr && scene->is_object())
            {
                const std::string details = scene->value("speakDetails", std::string{});
                if (details.empty() && scene->contains("actions")
                    && (*scene)["actions"].is_object())
                {
                    (*scene)["actions"]["speak"] = false;
                }
            }
        }
        docs.markDirty();
    }
    catch (const nlohmann::json::exception& ex)
    {
        errorOut = std::string("JSON mutate failed: ") + ex.what();
        return SpeakConversationDeleteStatus::MutateFailed;
    }
    return SpeakConversationDeleteStatus::Ok;
}

const char* speakConversationCreateStatusMessage(SpeakConversationCreateStatus status)
{
    switch (status)
    {
    case SpeakConversationCreateStatus::Ok:
        return "Ok";
    case SpeakConversationCreateStatus::SceneMissing:
        return "Scene missing";
    case SpeakConversationCreateStatus::ConversationsNotLoaded:
        return "Conversations not loaded";
    case SpeakConversationCreateStatus::AlreadyHasPhases:
        return "Already has speakPhases";
    case SpeakConversationCreateStatus::InvalidIds:
        return "Invalid ids";
    case SpeakConversationCreateStatus::EmptyIntro:
        return "Empty intro";
    case SpeakConversationCreateStatus::InvalidChoice:
        return "Invalid choice";
    case SpeakConversationCreateStatus::MutateFailed:
        return "Mutate failed";
    }
    return "Unknown";
}

const char* speakConversationDeleteStatusMessage(SpeakConversationDeleteStatus status)
{
    switch (status)
    {
    case SpeakConversationDeleteStatus::Ok:
        return "Ok";
    case SpeakConversationDeleteStatus::SceneMissing:
        return "Scene missing";
    case SpeakConversationDeleteStatus::ConversationsNotLoaded:
        return "Conversations not loaded";
    case SpeakConversationDeleteStatus::NothingToDelete:
        return "Nothing to delete";
    case SpeakConversationDeleteStatus::MutateFailed:
        return "Mutate failed";
    }
    return "Unknown";
}

std::string speakParentSceneId(const std::string& mapNodeId)
{
    std::string parent;
    std::string sub;
    timberline_engine::SceneDocument::parseMapNodeId(mapNodeId, parent, sub);
    return parent.empty() ? mapNodeId : parent;
}

} // namespace timberline_editor
