#include <MilestoneManager.h>
#include <SaveGame.h>

namespace testgame
{

namespace
{

MilestoneRuntimeEntry& runtimeEntry(MilestonePersistState& state, const std::string& milestoneId)
{
    return state.entries[milestoneId];
}

const MilestoneRuntimeEntry* findRuntimeEntry(
    const MilestonePersistState& state,
    const std::string& milestoneId)
{
    std::map<std::string, MilestoneRuntimeEntry>::const_iterator it = state.entries.find(milestoneId);
    if (it == state.entries.end())
        return nullptr;
    return &it->second;
}

bool isTerminalStatus(MilestoneStatus status)
{
    return status == MilestoneStatus::Complete || status == MilestoneStatus::Failed;
}

}

MilestoneManager::MilestoneManager(const MilestoneDatabase& databaseRef)
    : database(databaseRef)
{
}

MilestoneStatus MilestoneManager::getStatus(const std::string& milestoneId) const
{
    const MilestoneRuntimeEntry* entry = findRuntimeEntry(state, milestoneId);
    if (!entry)
        return MilestoneStatus::NotStarted;
    return entry->status;
}

bool MilestoneManager::isRequirementMet(const std::string& milestoneId) const
{
    return getStatus(milestoneId) == MilestoneStatus::Complete;
}

bool MilestoneManager::isTerminal(const std::string& milestoneId) const
{
    return isTerminalStatus(getStatus(milestoneId));
}

bool MilestoneManager::requirementsMet(
    const MilestoneDef& def,
    const MilestoneEventContext& context) const
{
    for (const std::string& requiredId : def.requires)
    {
        if (!context.getMilestoneStatus)
            return false;

        if (context.getMilestoneStatus(requiredId) != MilestoneStatus::Complete)
            return false;
    }

    return true;
}

bool MilestoneManager::conditionMet(
    const MilestoneCondition& condition,
    const MilestoneEventContext& context) const
{
    switch (condition.type)
    {
    case MilestoneCondition::Type::None:
        return false;

    case MilestoneCondition::Type::All:
        for (const MilestoneCondition& child : condition.children)
        {
            if (!conditionMet(child, context))
                return false;
        }
        return !condition.children.empty();

    case MilestoneCondition::Type::Any:
        for (const MilestoneCondition& child : condition.children)
        {
            if (conditionMet(child, context))
                return true;
        }
        return false;

    case MilestoneCondition::Type::StoryFlag:
        return context.storyFlags.count(condition.value) > 0;

    case MilestoneCondition::Type::ConversationPhaseComplete:
        return context.conversation != nullptr
            && context.conversation->completedPhaseIds.count(condition.value) > 0;

    case MilestoneCondition::Type::ConversationChoiceConsumed:
        return context.conversation != nullptr
            && context.conversation->consumedScriptedChoiceIds.count(condition.value) > 0;

    case MilestoneCondition::Type::HasItem:
        return context.hasItem && context.hasItem(condition.value);

    case MilestoneCondition::Type::MilestoneComplete:
        return context.getMilestoneStatus
            && context.getMilestoneStatus(condition.value) == MilestoneStatus::Complete;

    case MilestoneCondition::Type::MilestoneStarted:
    {
        if (!context.getMilestoneStatus)
            return false;
        const MilestoneStatus status = context.getMilestoneStatus(condition.value);
        return status == MilestoneStatus::Started || status == MilestoneStatus::Complete;
    }

    case MilestoneCondition::Type::SceneEntered:
        return !condition.value.empty() && context.sceneId == condition.value;

    case MilestoneCondition::Type::NpcDead:
        return context.deadNpcIds.count(condition.value) > 0;

    case MilestoneCondition::Type::TimeExpired:
        return false;
    }

    return false;
}

bool MilestoneManager::timeLimitExpired(
    const MilestoneDef& def,
    const MilestoneRuntimeEntry& entry,
    const MilestoneEventContext& context) const
{
    if (!def.timeLimit.isValid() || !entry.startedAt.isValid() || !context.time.isAvailable())
        return false;

    if (def.timeLimit.unit == MilestoneTimeUnit::GameDays)
        return context.time.gameDay > entry.startedAt.gameDay + def.timeLimit.amount;

    if (def.timeLimit.unit == MilestoneTimeUnit::GameHours)
    {
        const int startedHour = entry.startedAt.gameHour >= 0 ? entry.startedAt.gameHour : 0;
        const int currentHour = context.time.gameHour >= 0 ? context.time.gameHour : 0;
        const int startedTotal = entry.startedAt.gameDay * 24 + startedHour;
        const int currentTotal = context.time.gameDay * 24 + currentHour;
        return currentTotal - startedTotal >= def.timeLimit.amount;
    }

    return false;
}

void MilestoneManager::setStatus(
    const std::string& milestoneId,
    MilestoneStatus status,
    const MilestoneEventContext& context)
{
    MilestoneRuntimeEntry& entry = runtimeEntry(state, milestoneId);
    if (entry.status == status)
        return;

    entry.status = status;

    if (status != MilestoneStatus::Started)
        return;

    const MilestoneDef* def = database.find(milestoneId);
    if (!def || !def->timeLimit.isValid() || !context.time.isAvailable())
        return;

    entry.startedAt.gameDay = context.time.gameDay;
    entry.startedAt.gameHour = context.time.gameHour;
}

void MilestoneManager::updateObjectives(
    const MilestoneDef& def,
    MilestoneRuntimeEntry& entry,
    const MilestoneEventContext& context)
{
    if (entry.status != MilestoneStatus::Started && entry.status != MilestoneStatus::Complete)
        return;

    for (const MilestoneObjectiveDef& objective : def.objectives)
    {
        if (entry.completedObjectiveIds.count(objective.id) > 0)
            continue;

        if (conditionMet(objective.completeWhen, context))
            entry.completedObjectiveIds.insert(objective.id);
    }
}

void MilestoneManager::evaluateMilestone(
    const MilestoneDef& def,
    const MilestoneEventContext& context)
{
    MilestoneRuntimeEntry& entry = runtimeEntry(state, def.id);
    if (isTerminalStatus(entry.status))
        return;

    if (entry.status == MilestoneStatus::Started)
    {
        if (def.failWhen.type != MilestoneCondition::Type::None && conditionMet(def.failWhen, context))
        {
            entry.status = MilestoneStatus::Failed;
            return;
        }

        if (timeLimitExpired(def, entry, context))
        {
            entry.status = MilestoneStatus::Failed;
            return;
        }

        if (conditionMet(def.completeWhen, context))
        {
            entry.status = MilestoneStatus::Complete;
            updateObjectives(def, entry, context);
            return;
        }

        updateObjectives(def, entry, context);
        return;
    }

    if (entry.status != MilestoneStatus::NotStarted)
        return;

    if (!requirementsMet(def, context))
        return;

    if (conditionMet(def.completeWhen, context))
    {
        entry.status = MilestoneStatus::Complete;
        updateObjectives(def, entry, context);
        return;
    }

    const bool hasStartCondition = def.startWhen.type != MilestoneCondition::Type::None;
    if (!hasStartCondition || !conditionMet(def.startWhen, context))
        return;

    setStatus(def.id, MilestoneStatus::Started, context);
    updateObjectives(def, runtimeEntry(state, def.id), context);

    if (conditionMet(def.completeWhen, context))
        runtimeEntry(state, def.id).status = MilestoneStatus::Complete;
}

void MilestoneManager::evaluate(const MilestoneEventContext& context)
{
    bool changed = true;
    int guard = 0;

    while (changed && guard < 16)
    {
        changed = false;
        ++guard;

        for (const std::pair<const std::string, MilestoneDef>& pair : database.getMilestones())
        {
            const MilestoneStatus before = getStatus(pair.first);
            evaluateMilestone(pair.second, context);
            if (getStatus(pair.first) != before)
                changed = true;
        }
    }
}

void MilestoneManager::syncFromLegacyState(
    const std::set<std::string>& storyFlags,
    const ConversationPersistState& conversation,
    const std::function<bool(const std::string& itemId)>& hasItem)
{
    MilestoneEventContext context;
    context.storyFlags = storyFlags;
    context.conversation = &conversation;
    context.hasItem = hasItem;
    context.getMilestoneStatus = [this](const std::string& milestoneId)
    {
        return getStatus(milestoneId);
    };

    for (const std::pair<const std::string, MilestoneDef>& pair : database.getMilestones())
    {
        MilestoneRuntimeEntry& entry = runtimeEntry(state, pair.first);
        if (isTerminalStatus(entry.status))
            continue;

        if (conditionMet(pair.second.completeWhen, context))
            entry.status = MilestoneStatus::Complete;
    }

    evaluate(context);
}

bool MilestoneManager::tryStart(const std::string& milestoneId, const MilestoneEventContext& context)
{
    const MilestoneDef* def = database.find(milestoneId);
    if (!def || getStatus(milestoneId) != MilestoneStatus::NotStarted)
        return false;

    if (!requirementsMet(*def, context))
        return false;

    setStatus(milestoneId, MilestoneStatus::Started, context);
    evaluate(context);
    return true;
}

std::vector<const MilestoneDef*> MilestoneManager::getVisibleQuests() const
{
    std::vector<const MilestoneDef*> quests;
    quests.reserve(database.getMilestones().size());

    for (const std::pair<const std::string, MilestoneDef>& pair : database.getMilestones())
    {
        if (!pair.second.isQuest)
            continue;

        const MilestoneStatus status = getStatus(pair.first);
        if (status == MilestoneStatus::NotStarted)
            continue;

        quests.push_back(&pair.second);
    }

    return quests;
}

void MilestoneManager::exportPersistState(MilestonePersistState& out) const
{
    out = state;
}

void MilestoneManager::importPersistState(const MilestonePersistState& imported)
{
    state = imported;
}

}