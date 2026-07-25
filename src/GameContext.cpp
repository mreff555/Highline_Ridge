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

#include "GameContext.h"

namespace timberline_engine
{

MilestoneEventContext GameContext::makeMilestoneContext(
    ConversationPersistState& conversationSnapshot,
    const std::set<std::string>& storyFlags,
    const ConversationManager& conversationMgr,
    const std::string& sceneId,
    const MilestoneManager& milestoneMgr,
    const std::function<bool(const std::string& itemId)>& hasItem)
{
    conversationMgr.exportPersistState(conversationSnapshot);

    MilestoneEventContext context;
    context.storyFlags = storyFlags;
    context.conversation = &conversationSnapshot;
    context.sceneId = sceneId;
    context.hasItem = hasItem;
    context.getMilestoneStatus = [&milestoneMgr](const std::string& milestoneId)
    {
        return milestoneMgr.getStatus(milestoneId);
    };
    return context;
}

}