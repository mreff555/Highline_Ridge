#ifndef MILESTONE_MANAGER_H
#define MILESTONE_MANAGER_H

#include <MilestoneDatabase.h>
#include <MilestoneStruct.h>
#include <vector>

namespace testgame
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