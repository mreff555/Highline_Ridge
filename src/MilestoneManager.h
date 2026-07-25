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

#ifndef MILESTONE_MANAGER_H
#define MILESTONE_MANAGER_H

#include <MilestoneDatabase.h>
#include <MilestoneStruct.h>
#include <vector>

namespace timberline_engine
{

class MilestoneManager
{
public:
    explicit MilestoneManager(const MilestoneDatabase& database);

    MilestoneStatus getStatus(const std::string& milestoneId) const;
    bool isRequirementMet(const std::string& milestoneId) const;
    bool isTerminal(const std::string& milestoneId) const;

    void evaluate(const MilestoneEventContext& context);
    void syncFromLegacyState(
        const std::set<std::string>& storyFlags,
        const ConversationPersistState& conversation,
        const std::function<bool(const std::string& itemId)>& hasItem);

    bool tryStart(const std::string& milestoneId, const MilestoneEventContext& context);

    std::vector<const MilestoneDef*> getVisibleQuests() const;

    void exportPersistState(MilestonePersistState& out) const;
    void importPersistState(const MilestonePersistState& state);

private:
    bool requirementsMet(const MilestoneDef& def, const MilestoneEventContext& context) const;
    bool conditionMet(const MilestoneCondition& condition, const MilestoneEventContext& context) const;
    bool timeLimitExpired(
        const MilestoneDef& def,
        const MilestoneRuntimeEntry& entry,
        const MilestoneEventContext& context) const;

    void setStatus(const std::string& milestoneId, MilestoneStatus status, const MilestoneEventContext& context);
    void updateObjectives(
        const MilestoneDef& def,
        MilestoneRuntimeEntry& entry,
        const MilestoneEventContext& context);
    void evaluateMilestone(const MilestoneDef& def, const MilestoneEventContext& context);

    const MilestoneDatabase& database;
    MilestonePersistState state;
};

}

#endif /* MILESTONE_MANAGER_H */