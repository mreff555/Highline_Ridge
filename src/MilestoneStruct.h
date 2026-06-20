#ifndef MILESTONE_STRUCT_H
#define MILESTONE_STRUCT_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace testgame
{

struct ConversationPersistState;

enum class MilestoneStatus
{
    NotStarted,
    Started,
    Complete,
    Failed
};

enum class MilestoneTimeUnit
{
    None,
    GameDays,
    GameHours
};

struct MilestoneTimeLimit
{
    int amount = 0;
    MilestoneTimeUnit unit = MilestoneTimeUnit::None;

    bool isValid() const { return amount > 0 && unit != MilestoneTimeUnit::None; }
};

struct MilestoneGameTime
{
    int gameDay = -1;
    int gameHour = -1;

    bool isValid() const { return gameDay >= 0; }
};

struct MilestoneCondition
{
    enum class Type
    {
        None,
        All,
        Any,
        StoryFlag,
        ConversationPhaseComplete,
        ConversationChoiceConsumed,
        HasItem,
        MilestoneComplete,
        MilestoneStarted,
        SceneEntered,
        NpcDead,
        TimeExpired
    };

    Type type = Type::None;
    std::string value;
    std::vector<MilestoneCondition> children;
};

struct MilestoneObjectiveDef
{
    std::string id;
    std::string text;
    bool required = false;
    MilestoneCondition completeWhen;
};

struct MilestoneDef
{
    std::string id;
    bool isQuest = false;
    std::string title;
    std::string summary;
    std::string completeSummary;
    std::vector<std::string> requires;
    MilestoneCondition startWhen;
    MilestoneCondition completeWhen;
    MilestoneCondition failWhen;
    MilestoneTimeLimit timeLimit;
    std::vector<MilestoneObjectiveDef> objectives;
};

struct MilestoneRuntimeEntry
{
    MilestoneStatus status = MilestoneStatus::NotStarted;
    std::set<std::string> completedObjectiveIds;
    MilestoneGameTime startedAt;
};

struct MilestonePersistState
{
    std::map<std::string, MilestoneRuntimeEntry> entries;
};

struct MilestoneTimeContext
{
    int gameDay = -1;
    int gameHour = -1;

    bool isAvailable() const { return gameDay >= 0; }
};

struct MilestoneEventContext
{
    std::set<std::string> storyFlags;
    const ConversationPersistState* conversation = nullptr;
    std::function<bool(const std::string& itemId)> hasItem;
    std::function<MilestoneStatus(const std::string& milestoneId)> getMilestoneStatus;
    std::string sceneId;
    MilestoneTimeContext time;
    std::set<std::string> deadNpcIds;
};

}

#endif /* MILESTONE_STRUCT_H */