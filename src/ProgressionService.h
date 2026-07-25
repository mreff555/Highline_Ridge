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

#ifndef PROGRESSION_SERVICE_H
#define PROGRESSION_SERVICE_H

#include <MilestoneManager.h>
#include <SaveGame.h>
#include <set>
#include <string>

namespace timberline_engine
{

class ConversationManager;

class ProgressionService
{
    public:
    ProgressionService(MilestoneManager& milestoneMgr);

    void evaluate(
        const std::set<std::string>& storyFlags,
        const ConversationManager& conversationMgr,
        const std::string& sceneId,
        const std::function<bool(const std::string& itemId)>& hasItem);

    void syncFromLegacyState(
        const std::set<std::string>& storyFlags,
        const ConversationPersistState& conversation,
        const std::function<bool(const std::string& itemId)>& hasItem);

    bool hasStoryFlag(const std::set<std::string>& storyFlags, const std::string& flag) const;
    MilestoneStatus getMilestoneStatus(const std::string& milestoneId) const;
    std::vector<const MilestoneDef*> getVisibleQuests() const;

    private:
    MilestoneManager& milestoneMgr;
};

}

#endif /* PROGRESSION_SERVICE_H */