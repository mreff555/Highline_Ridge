/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "ConditionMarkup.h"

#include <InventoryMgr.h>
#include <MilestoneStruct.h>

#include <cctype>
#include <vector>

namespace timberline_engine
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

std::string toLowerCopy(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char ch : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

std::vector<std::string> splitColon(const std::string& s)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : s)
    {
        if (ch == ':')
        {
            parts.push_back(cur);
            cur.clear();
        }
        else
            cur.push_back(ch);
    }
    parts.push_back(cur);
    return parts;
}

bool storyFlagSet(const AuthorConditionContext& context, const std::string& flag)
{
    return context.mask != nullptr && context.mask->storyFlags != nullptr
        && context.mask->storyFlags->count(flag) > 0;
}

bool playerHasItem(const AuthorConditionContext& context, const std::string& itemId)
{
    if (context.mask == nullptr || itemId.empty())
        return false;
    if (context.mask->inventoryMgr != nullptr && context.mask->inventoryMgr->hasItem(itemId))
        return true;
    return context.mask->sceneInventoryHasItem && context.mask->sceneInventoryHasItem(itemId);
}

bool milestoneSet(const AuthorConditionContext& context, const std::string& milestoneId)
{
    if (context.mask == nullptr || context.mask->milestoneMgr == nullptr || milestoneId.empty())
        return false;
    const MilestoneStatus status = context.mask->milestoneMgr->getStatus(milestoneId);
    return status == MilestoneStatus::Complete || status == MilestoneStatus::Started;
}

bool sceneExamined(const AuthorConditionContext& context, const std::string& sceneId)
{
    return context.examinedSceneIds != nullptr && !sceneId.empty()
        && context.examinedSceneIds->count(sceneId) > 0;
}

bool sceneVisited(const AuthorConditionContext& context, const std::string& sceneId)
{
    if (sceneId.empty())
        return false;
    if (context.visitedSceneIds != nullptr && context.visitedSceneIds->count(sceneId) > 0)
        return true;
    // Examined implies visited.
    return sceneExamined(context, sceneId);
}

bool itemDiscovered(const AuthorConditionContext& context, const std::string& itemId)
{
    if (itemId.empty())
        return false;
    if (playerHasItem(context, itemId))
        return true;
    if (context.itemDiscovered)
        return context.itemDiscovered(itemId);
    return false;
}

bool actorStateTrue(
    const AuthorConditionContext& context,
    const std::string& actorId,
    const std::string& state)
{
    if (actorId.empty() || state.empty())
        return false;
    if (context.actorState)
        return context.actorState(actorId, state);
    if (state == "observed")
        return context.knownActorIds != nullptr && context.knownActorIds->count(actorId) > 0;
    // spoken_to / attacked via story flag convention when no callback.
    return storyFlagSet(context, "actor:" + actorId + ":" + state);
}

/** Apply universal not_ on object or state token. */
bool applyNegation(bool value, bool negated)
{
    return negated ? !value : value;
}

} // namespace

std::string normalizeAuthorCondition(const std::string& raw)
{
    std::string s = trimCopy(raw);
    if (s.empty())
        return {};

    // Sugar: {{condition:…}} or {{not_condition:…}}
    if (s.size() >= 4 && s.rfind("{{", 0) == 0 && s.size() >= 2
        && s.compare(s.size() - 2, 2, "}}") == 0)
    {
        s = trimCopy(s.substr(2, s.size() - 4));
    }

    const std::string lower = toLowerCopy(s);
    const std::string condPrefix = "condition:";
    const std::string notCondPrefix = "not_condition:";
    if (lower.rfind(notCondPrefix, 0) == 0)
        s = "not_" + trimCopy(s.substr(notCondPrefix.size()));
    else if (lower.rfind(condPrefix, 0) == 0)
        s = trimCopy(s.substr(condPrefix.size()));

    return trimCopy(s);
}

bool isConditionOpenBraceBody(const std::string& body)
{
    const std::string lower = toLowerCopy(trimCopy(body));
    return lower.rfind("condition:", 0) == 0 || lower.rfind("not_condition:", 0) == 0;
}

bool isConditionCloseBraceBody(const std::string& body)
{
    const std::string lower = toLowerCopy(trimCopy(body));
    return lower == "/condition";
}

bool evaluateAuthorCondition(const std::string& when, const AuthorConditionContext& context)
{
    const std::string normalized = normalizeAuthorCondition(when);
    if (normalized.empty())
        return true; // default branch

    std::string clause = toLowerCopy(normalized);
    bool negated = false;
    if (clause.rfind("not_", 0) == 0)
    {
        negated = true;
        clause = clause.substr(4);
    }

    // Also support object:id:not_state → negate state.
    std::vector<std::string> parts = splitColon(clause);
    if (parts.size() < 2)
        return false;

    const std::string object = parts[0];

    // item:<id>:in_inventory | item:<id>:discovered | item:<id>:not_in_inventory
    if (object == "item")
    {
        if (parts.size() < 3)
            return false;
        const std::string itemId = parts[1];
        std::string state = parts[2];
        for (size_t i = 3; i < parts.size(); ++i)
            state += ":" + parts[i];
        bool stateNeg = false;
        if (state.rfind("not_", 0) == 0)
        {
            stateNeg = true;
            state = state.substr(4);
        }
        bool ok = false;
        if (state == "in_inventory")
            ok = playerHasItem(context, itemId);
        else if (state == "discovered")
            ok = itemDiscovered(context, itemId);
        else
            return false;
        return applyNegation(applyNegation(ok, stateNeg), negated);
    }

    // flag:<id>:set | flag:<id>:not_set
    if (object == "flag")
    {
        if (parts.size() < 3)
            return false;
        const std::string flagId = parts[1];
        std::string state = parts[2];
        bool stateNeg = false;
        if (state.rfind("not_", 0) == 0)
        {
            stateNeg = true;
            state = state.substr(4);
        }
        if (state != "set")
            return false;
        const bool ok = storyFlagSet(context, flagId);
        return applyNegation(applyNegation(ok, stateNeg), negated);
    }

    // milestone:<id>:set
    if (object == "milestone")
    {
        if (parts.size() < 3)
            return false;
        const std::string milestoneId = parts[1];
        std::string state = parts[2];
        bool stateNeg = false;
        if (state.rfind("not_", 0) == 0)
        {
            stateNeg = true;
            state = state.substr(4);
        }
        if (state != "set")
            return false;
        const bool ok = milestoneSet(context, milestoneId);
        return applyNegation(applyNegation(ok, stateNeg), negated);
    }

    // scene:<id>:visited|examined|not_visited|not_examined
    if (object == "scene")
    {
        if (parts.size() < 3)
            return false;
        const std::string sceneId = parts[1];
        std::string state = parts[2];
        bool stateNeg = false;
        if (state.rfind("not_", 0) == 0)
        {
            stateNeg = true;
            state = state.substr(4);
        }
        bool ok = false;
        if (state == "visited")
            ok = sceneVisited(context, sceneId);
        else if (state == "examined")
            ok = sceneExamined(context, sceneId);
        else
            return false;
        return applyNegation(applyNegation(ok, stateNeg), negated);
    }

    // actor:<id>:observed|spoken_to|attacked
    if (object == "actor")
    {
        if (parts.size() < 3)
            return false;
        const std::string actorId = parts[1];
        std::string state = parts[2];
        bool stateNeg = false;
        if (state.rfind("not_", 0) == 0)
        {
            stateNeg = true;
            state = state.substr(4);
        }
        if (state != "observed" && state != "spoken_to" && state != "attacked")
            return false;
        const bool ok = actorStateTrue(context, actorId, state);
        return applyNegation(applyNegation(ok, stateNeg), negated);
    }

    return false;
}

} // namespace timberline_engine
