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

#include "MaskEvaluator.h"

#include <InventoryMgr.h>
#include <ItemDatabase.h>
#include <InventoryItem.h>

namespace timberline_engine
{

namespace
{

bool playerHasTopLevelItemDef(
    const InventoryMgr* inventoryMgr,
    const std::string& itemDefId)
{
    if (inventoryMgr == nullptr || itemDefId.empty())
        return false;

    return inventoryMgr->hasItem(itemDefId);
}

bool milestoneMatches(
    const MilestoneManager* milestoneMgr,
    const std::string& milestoneId,
    MilestoneStatus expected)
{
    if (milestoneMgr == nullptr || milestoneId.empty())
        return false;

    return milestoneMgr->getStatus(milestoneId) == expected;
}

}

bool evaluateMaskCondition(const MaskCondition& condition, const MaskEvalContext& context)
{
    switch (condition.type)
    {
        case MaskConditionType::None:
            return true;
        case MaskConditionType::All:
        {
            for (const MaskCondition& child : condition.children)
            {
                if (!evaluateMaskCondition(child, context))
                    return false;
            }
            return !condition.children.empty() || true;
        }
        case MaskConditionType::Any:
        {
            if (condition.children.empty())
                return false;
            for (const MaskCondition& child : condition.children)
            {
                if (evaluateMaskCondition(child, context))
                    return true;
            }
            return false;
        }
        case MaskConditionType::Flag:
            return context.storyFlags != nullptr
                && context.storyFlags->count(condition.value) > 0;
        case MaskConditionType::MilestoneStarted:
            return milestoneMatches(context.milestoneMgr, condition.value, MilestoneStatus::Started)
                || milestoneMatches(context.milestoneMgr, condition.value, MilestoneStatus::Complete);
        case MaskConditionType::MilestoneComplete:
            return milestoneMatches(context.milestoneMgr, condition.value, MilestoneStatus::Complete);
        case MaskConditionType::MilestoneFailed:
            return milestoneMatches(context.milestoneMgr, condition.value, MilestoneStatus::Failed);
        case MaskConditionType::PlayerHasItem:
            return playerHasTopLevelItemDef(context.inventoryMgr, condition.value);
        case MaskConditionType::PlayerHasItemFlag:
            return playerHasTopLevelItemFlag(context, condition.value);
        case MaskConditionType::SubSceneActive:
            return !condition.value.empty() && context.activeSubSceneId == condition.value;
        default:
            return false;
    }
}

bool isMaskConditionMet(const MaskCondition& condition, const MaskEvalContext& context)
{
    if (condition.type == MaskConditionType::None)
        return true;

    return evaluateMaskCondition(condition, context);
}

bool playerHasTopLevelItemFlag(
    const MaskEvalContext& context,
    const std::string& flag)
{
    if (context.inventoryMgr == nullptr || context.itemDatabase == nullptr || flag.empty())
        return false;

    for (const InventoryItem& item : context.inventoryMgr->exportItemSnapshots())
    {
        const ItemDef* def = context.itemDatabase->getDef(item.id);
        if (def == nullptr)
            continue;

        if (flag == "light_source" && def->lightSource)
            return true;

        if (hasItemFlag(item.instance.activeFlags, flag))
            return true;
    }

    return false;
}

}