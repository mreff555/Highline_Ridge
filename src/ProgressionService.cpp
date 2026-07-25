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

#include "ProgressionService.h"

#include <GameContext.h>

namespace timberline_engine
{

ProgressionService::ProgressionService(MilestoneManager& milestoneMgr_)
    : milestoneMgr(milestoneMgr_)
{
}

void ProgressionService::evaluate(
    const std::set<std::string>& storyFlags,
    const ConversationManager& conversationMgr,
    const std::string& sceneId,
    const std::function<bool(const std::string& itemId)>& hasItem)
{
    ConversationPersistState conversationSnapshot;
    const MilestoneEventContext context = GameContext::makeMilestoneContext(
        conversationSnapshot,
        storyFlags,
        conversationMgr,
        sceneId,
        milestoneMgr,
        hasItem);
    milestoneMgr.evaluate(context);
}

void ProgressionService::syncFromLegacyState(
    const std::set<std::string>& storyFlags,
    const ConversationPersistState& conversation,
    const std::function<bool(const std::string& itemId)>& hasItem)
{
    milestoneMgr.syncFromLegacyState(storyFlags, conversation, hasItem);
}

bool ProgressionService::hasStoryFlag(
    const std::set<std::string>& storyFlags,
    const std::string& flag) const
{
    return storyFlags.count(flag) > 0;
}

MilestoneStatus ProgressionService::getMilestoneStatus(const std::string& milestoneId) const
{
    return milestoneMgr.getStatus(milestoneId);
}

std::vector<const MilestoneDef*> ProgressionService::getVisibleQuests() const
{
    return milestoneMgr.getVisibleQuests();
}

}