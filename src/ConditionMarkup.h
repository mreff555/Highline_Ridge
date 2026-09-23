/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author condition strings for blockedVariants.when / {{condition:…}} sugar (#42 P3).
 * Not passed through parseVoiceMarkup — bags are selected at runtime instead.
 ******************************************************************************/

#ifndef CONDITION_MARKUP_H
#define CONDITION_MARKUP_H

#include <MaskEvaluator.h>

#include <functional>
#include <set>
#include <string>

namespace timberline_engine
{

/**
 * Extra world facts beyond MaskEvalContext for author condition strings.
 * Pointers may be null (treated as empty / false).
 */
struct AuthorConditionContext
{
    const MaskEvalContext* mask = nullptr;
    const std::set<std::string>* examinedSceneIds = nullptr;
    /** Scenes the player has entered this playthrough (best-effort). */
    const std::set<std::string>* visitedSceneIds = nullptr;
    const std::set<std::string>* knownActorIds = nullptr;
    /** True if item id was taken / discovered (inventory history). */
    std::function<bool(const std::string& itemId)> itemDiscovered;
    /**
     * Actor state: observed | spoken_to | attacked.
     * observed → knownActorIds; spoken_to/attacked → story flag
     * "actor:<id>:<state>" when present.
     */
    std::function<bool(const std::string& actorId, const std::string& state)> actorState;
};

/** Strip optional {{condition:…}} / whitespace; empty → empty. */
std::string normalizeAuthorCondition(const std::string& raw);

/**
 * Evaluate a single author condition clause.
 * Empty when → true (default variant branch).
 * Unknown / malformed → false.
 */
bool evaluateAuthorCondition(const std::string& when, const AuthorConditionContext& context);

/** True if brace body is a condition open tag (condition:… / not_condition:…). */
bool isConditionOpenBraceBody(const std::string& body);

/** True if brace body is {{/condition}}. */
bool isConditionCloseBraceBody(const std::string& body);

}

#endif /* CONDITION_MARKUP_H */
