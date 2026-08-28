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

#include "MovementResolver.h"

#include <ItemDatabase.h>
#include <ItemDef.h>
#include <InventoryMgr.h>
#include <raylib.h>

namespace timberline_engine
{

namespace
{

bool movementBlanketAllows(const MovementStruct& blanket, const std::string& direction)
{
    if (direction == "up")
        return blanket.up;
    if (direction == "down")
        return blanket.down;
    if (direction == "forward")
        return blanket.forward;
    if (direction == "backward")
        return blanket.backward;
    if (direction == "left")
        return blanket.left;
    if (direction == "right")
        return blanket.right;
    return false;
}

bool movementStructHasDirection(const MovementStruct& movement, const std::string& direction)
{
    return movementBlanketAllows(movement, direction);
}

const SubSceneDef* findSubScene(const SceneData& scene, const std::string& subSceneId)
{
    std::map<std::string, SubSceneDef>::const_iterator it = scene.subScenes.find(subSceneId);
    if (it == scene.subScenes.end())
        return nullptr;
    return &it->second;
}

bool sceneStateProvidesLight(
    const SceneData& scene,
    const std::string& activeSubSceneId,
    const MaskEvalContext& context)
{
    const SubSceneDef* subScene = findSubScene(scene, activeSubSceneId);
    if (subScene != nullptr && subScene->providesLight())
        return true;

    if (context.storyFlags == nullptr)
        return false;

    const std::string prefix = scene.id + ":";
    for (const std::string& flag : *context.storyFlags)
    {
        if (flag.find(prefix) == 0 && flag.find("lanterns_lit") != std::string::npos)
            return true;
    }

    return false;
}

bool milestoneHookActive(
    const MovementMappingDef& mapping,
    const MilestoneManager* milestoneMgr,
    bool enabling)
{
    if (milestoneMgr == nullptr)
        return false;

    const std::vector<MilestoneMaskHook>& hooks = enabling
        ? mapping.onMilestoneEnable
        : mapping.onMilestoneDisable;

    for (const MilestoneMaskHook& hook : hooks)
    {
        if (milestoneMgr->getStatus(hook.milestoneId) == hook.when)
            return true;
    }

    return false;
}

bool itemDefIsLightSource(const MaskEvalContext& context, const std::string& itemId)
{
    if (context.itemDatabase == nullptr || itemId.empty())
        return false;
    const ItemDef* def = context.itemDatabase->getDef(itemId);
    return def != nullptr && def->lightSource;
}

bool playerHasItemDef(const MaskEvalContext& context, const std::string& itemId)
{
    if (context.inventoryMgr == nullptr || itemId.empty())
        return false;
    return context.inventoryMgr->hasItem(itemId);
}

bool storyFlagSet(const MaskEvalContext& context, const std::string& flag)
{
    return context.storyFlags != nullptr && !flag.empty()
        && context.storyFlags->count(flag) > 0;
}

bool maskConditionIsLightRequirement(
    const MaskCondition& condition,
    const MaskEvalContext& context)
{
    if (condition.type == MaskConditionType::PlayerHasItemFlag)
        return condition.value == "light_source";
    if (condition.type == MaskConditionType::PlayerHasItem)
        return itemDefIsLightSource(context, condition.value);
    return false;
}

}

MovementBlockReason MovementResolver::blockReasonForDirection(
    const SceneDatabase& database,
    const SceneData& scene,
    const std::string& activeSubSceneId,
    const std::string& direction,
    const MaskEvalContext& context)
{
    const MovementResolution resolved = resolveDirection(
        database, scene, activeSubSceneId, direction, context);
    if (resolved.available)
        return MovementBlockReason::None;

    std::map<std::string, std::vector<MovementMappingDef>>::const_iterator mappingsIt =
        scene.movementExits.find(direction);
    if (mappingsIt == scene.movementExits.end() || mappingsIt->second.empty())
        return MovementBlockReason::None;

    if (!isDirectionBlanketed(scene, activeSubSceneId, direction))
        return MovementBlockReason::None;

    bool needsLight = false;
    bool otherBlock = false;

    ExitRequirementDef requirement;
    if (database.getExitRequirement(scene.id, direction, requirement))
    {
        if (requirement.requiresLightSource
            && !playerHasTopLevelItemFlag(context, "light_source"))
            needsLight = true;

        if (!requirement.requiresInventoryItem.empty()
            && !playerHasItemDef(context, requirement.requiresInventoryItem))
        {
            if (itemDefIsLightSource(context, requirement.requiresInventoryItem))
                needsLight = true;
            else
                otherBlock = true;
        }

        if (!requirement.requiresInventoryItems.empty())
        {
            bool missingLight = false;
            bool missingOther = false;
            for (const std::string& itemId : requirement.requiresInventoryItems)
            {
                if (playerHasItemDef(context, itemId))
                    continue;
                if (itemDefIsLightSource(context, itemId))
                    missingLight = true;
                else
                    missingOther = true;
            }
            if (missingOther)
                otherBlock = true;
            else if (missingLight)
                needsLight = true;
        }

        if (requirement.requiresRoomPurchasedToday)
            otherBlock = true;

        if (!requirement.requiresStoryFlag.empty()
            && !storyFlagSet(context, requirement.requiresStoryFlag))
            otherBlock = true;
    }

    const bool illuminated = hasIllumination(scene, activeSubSceneId, context);
    for (const MovementMappingDef& mapping : mappingsIt->second)
    {
        if (isMappingEffectivelyMasked(mapping, context))
        {
            if (maskConditionIsLightRequirement(mapping.unmaskWhen, context)
                && !isMaskConditionMet(mapping.unmaskWhen, context))
                needsLight = true;
            else if (mapping.defaultMasked
                && !isMaskConditionMet(mapping.unmaskWhen, context)
                && !maskConditionIsLightRequirement(mapping.unmaskWhen, context))
                otherBlock = true;
            continue;
        }

        if (!illuminated && targetRequiresLight(database, mapping.target))
            needsLight = true;
    }

    // Darkness badge only when light is the blocking reason — not when another
    // lock would still keep the exit closed even with a lantern.
    if (needsLight && !otherBlock)
        return MovementBlockReason::NeedsLight;
    if (needsLight || otherBlock)
        return MovementBlockReason::Other;
    return MovementBlockReason::Other;
}

bool MovementResolver::isDirectionBlanketed(
    const SceneData& scene,
    const std::string& activeSubSceneId,
    const std::string& direction)
{
    const SubSceneDef* subScene = findSubScene(scene, activeSubSceneId);
    if (subScene == nullptr)
        return true;

    if (!movementStructHasDirection(subScene->movement, direction))
        return false;

    return movementBlanketAllows(subScene->movementBlanket, direction);
}

bool MovementResolver::isMappingEffectivelyMasked(
    const MovementMappingDef& mapping,
    const MaskEvalContext& context)
{
    bool masked = mapping.defaultMasked;

    if (isMaskConditionMet(mapping.unmaskWhen, context))
        masked = false;

    if (milestoneHookActive(mapping, context.milestoneMgr, true))
        masked = false;

    if (milestoneHookActive(mapping, context.milestoneMgr, false))
        masked = true;

    return masked;
}

bool MovementResolver::targetRequiresLight(
    const SceneDatabase& database,
    const MovementTarget& target)
{
    if (target.empty())
        return false;

    const SceneData* targetScene = database.getScene(target.sceneId);
    if (targetScene == nullptr)
        return false;

    std::string subSceneId = target.subSceneId;
    if (subSceneId.empty())
        subSceneId = targetScene->defaultSubSceneId;

    const SubSceneDef* subScene = findSubScene(*targetScene, subSceneId);
    if (subScene == nullptr)
        return false;

    return subScene->isDark();
}

bool MovementResolver::hasIllumination(
    const SceneData& scene,
    const std::string& activeSubSceneId,
    const MaskEvalContext& context)
{
    if (sceneStateProvidesLight(scene, activeSubSceneId, context))
        return true;

    return playerHasTopLevelItemFlag(context, "light_source");
}

MovementResolution MovementResolver::resolveDirection(
    const SceneDatabase& database,
    const SceneData& scene,
    const std::string& activeSubSceneId,
    const std::string& direction,
    const MaskEvalContext& context)
{
    MovementResolution resolution;

    std::map<std::string, std::vector<MovementMappingDef>>::const_iterator mappingsIt =
        scene.movementExits.find(direction);
    if (mappingsIt == scene.movementExits.end() || mappingsIt->second.empty())
        return resolution;

    if (!isDirectionBlanketed(scene, activeSubSceneId, direction))
        return resolution;

    const bool illuminated = hasIllumination(scene, activeSubSceneId, context);

    std::vector<const MovementMappingDef*> unmasked;
    for (const MovementMappingDef& mapping : mappingsIt->second)
    {
        if (isMappingEffectivelyMasked(mapping, context))
            continue;

        if (!illuminated && targetRequiresLight(database, mapping.target))
            continue;

        unmasked.push_back(&mapping);
    }

    if (unmasked.empty())
        return resolution;

    const MovementMappingDef* chosen = unmasked.front();
    if (unmasked.size() > 1)
    {
        std::vector<const MovementMappingDef*> conditional;
        std::vector<const MovementMappingDef*> unconditional;
        conditional.reserve(unmasked.size());
        unconditional.reserve(unmasked.size());
        for (const MovementMappingDef* candidate : unmasked)
        {
            if (candidate->defaultMasked)
                conditional.push_back(candidate);
            else
                unconditional.push_back(candidate);
        }

        if (!conditional.empty())
            chosen = conditional.front();
        else
            chosen = unconditional.front();

        const bool stillAmbiguous = conditional.size() > 1
            || (conditional.empty() && unconditional.size() > 1);
        if (stillAmbiguous)
        {
            TraceLog(
                LOG_WARNING,
                "Movement mask ambiguity in scene '%s' direction '%s': %d unmasked mappings; using '%s'",
                scene.id.c_str(),
                direction.c_str(),
                (int)unmasked.size(),
                chosen->id.c_str());
        }
    }

    resolution.available = true;
    resolution.target = chosen->target;
    resolution.mappingId = chosen->id;
    return resolution;
}

}