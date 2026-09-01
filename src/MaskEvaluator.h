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

#ifndef MASK_EVALUATOR_H
#define MASK_EVALUATOR_H

#include <MaskConditionDef.h>
#include <MilestoneManager.h>
#include <functional>
#include <set>
#include <string>

namespace timberline_engine
{

class InventoryMgr;
class ItemDatabase;

struct MaskEvalContext
{
    const std::set<std::string>* storyFlags = nullptr;
    const MilestoneManager* milestoneMgr = nullptr;
    const InventoryMgr* inventoryMgr = nullptr;
    const ItemDatabase* itemDatabase = nullptr;
    std::string activeSubSceneId;
    std::function<bool(const std::string& itemDefId)> sceneInventoryHasItem;
    std::function<bool(const std::string& itemFlag)> sceneInventoryHasItemFlag;
    /** Used by exitRequirements.requiresRoomPurchasedToday. */
    int currentDay = 0;
    int saloonRoomPurchasedDay = 0;

    bool roomPurchasedToday() const
    {
        return saloonRoomPurchasedDay > 0 && saloonRoomPurchasedDay == currentDay;
    }
};

bool evaluateMaskCondition(const MaskCondition& condition, const MaskEvalContext& context);
bool isMaskConditionMet(const MaskCondition& condition, const MaskEvalContext& context);
bool playerHasTopLevelItemFlag(
    const MaskEvalContext& context,
    const std::string& flag);

}

#endif /* MASK_EVALUATOR_H */