/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Phase A: Create / delete Speak conversation scaffolds for empty scenes.
 ******************************************************************************/

#ifndef TIMBERLINE_SPEAK_CONVERSATION_CREATE_H
#define TIMBERLINE_SPEAK_CONVERSATION_CREATE_H

#include "DocumentWorkspace.h"

#include <string>

namespace timberline_editor
{

struct SpeakConversationCreatePayload
{
    std::string sceneId; // locked
    std::string actorId; // sanitized; default "npc"
    std::string actorName; // display
    std::string phaseId; // sanitized; default == actorId
    std::string intro; // required non-empty
    std::string starterChoiceId; // default "player_opener"
    std::string starterChoiceLabel; // default "What brings you here?"
    bool openActorDialogAfter = true;
};

enum class SpeakConversationCreateStatus
{
    Ok,
    SceneMissing,
    ConversationsNotLoaded,
    AlreadyHasPhases,
    InvalidIds,
    EmptyIntro,
    InvalidChoice,
    MutateFailed
};

enum class SpeakConversationDeleteStatus
{
    Ok,
    SceneMissing,
    ConversationsNotLoaded,
    NothingToDelete,
    MutateFailed
};

/** True when conversationsRoot[sceneId].speakPhases is a non-empty array. */
bool sceneHasSpeakPhases(const DocumentWorkspace& docs, const std::string& sceneId);

/**
 * True when conversationsRoot has an object entry for sceneId (even with empty
 * speakPhases). False when the key is missing.
 */
bool sceneHasConversationEntry(const DocumentWorkspace& docs, const std::string& sceneId);

/** Sanitize actor/phase/choice ids (lowercase alnum + underscore). */
std::string sanitizeSpeakId(const std::string& raw);

/** Title-case actor display name from id (mirror of engine formatActorDisplayName). */
std::string formatSpeakActorDisplayName(const std::string& actorId);

/** Fill payload defaults for a scene (actor npc, placeholder intro, etc.). */
void fillSpeakConversationCreateDefaults(
    SpeakConversationCreatePayload& payload,
    const std::string& sceneId);

/**
 * In-memory scaffold: requires conversationsLoaded. Writes speakPhases + sets
 * actions.speak = true and markDirty(). Does not save to disk.
 */
SpeakConversationCreateStatus createSpeakConversationScaffold(
    DocumentWorkspace& docs,
    const SpeakConversationCreatePayload& payload,
    std::string& errorOut);

/**
 * Remove conversationsRoot[sceneId]. Optionally clear actions.speak when there
 * is no legacy speakDetails. Marks dirty; does not save.
 */
SpeakConversationDeleteStatus deleteSpeakConversation(
    DocumentWorkspace& docs,
    const std::string& sceneId,
    bool clearSpeakActionIfNoDetails,
    std::string& errorOut);

const char* speakConversationCreateStatusMessage(SpeakConversationCreateStatus status);
const char* speakConversationDeleteStatusMessage(SpeakConversationDeleteStatus status);

/** Parent scene id for map node ids (`parent` or `parent#sub` → `parent`). */
std::string speakParentSceneId(const std::string& mapNodeId);

} // namespace timberline_editor

#endif /* TIMBERLINE_SPEAK_CONVERSATION_CREATE_H */
