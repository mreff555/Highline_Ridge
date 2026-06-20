#include <MilestoneDatabase.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <raylib.h>

namespace testgame
{

namespace
{

MilestoneTimeUnit parseTimeUnit(const std::string& unit)
{
    if (unit == "game_days" || unit == "gameDays")
        return MilestoneTimeUnit::GameDays;
    if (unit == "game_hours" || unit == "gameHours")
        return MilestoneTimeUnit::GameHours;
    return MilestoneTimeUnit::None;
}

bool parseConditionObject(const nlohmann::json& object, MilestoneCondition& out);

bool parseConditionArray(const nlohmann::json& array, MilestoneCondition& out, MilestoneCondition::Type type)
{
    if (!array.is_array() || array.empty())
        return false;

    out.type = type;
    out.children.clear();
    out.children.reserve(array.size());

    for (const nlohmann::json& child : array)
    {
        MilestoneCondition parsed;
        if (!parseConditionObject(child, parsed))
            return false;
        out.children.push_back(parsed);
    }

    return true;
}

bool parseConditionObject(const nlohmann::json& object, MilestoneCondition& out)
{
    out = MilestoneCondition{};

    if (!object.is_object() || object.empty())
        return false;

    if (object.contains("all"))
        return parseConditionArray(object["all"], out, MilestoneCondition::Type::All);
    if (object.contains("any"))
        return parseConditionArray(object["any"], out, MilestoneCondition::Type::Any);

    struct NamedCondition
    {
        const char* key;
        MilestoneCondition::Type type;
    };

    static const NamedCondition kNamedConditions[] = {
        { "storyFlag", MilestoneCondition::Type::StoryFlag },
        { "conversationPhaseComplete", MilestoneCondition::Type::ConversationPhaseComplete },
        { "conversationChoiceConsumed", MilestoneCondition::Type::ConversationChoiceConsumed },
        { "hasItem", MilestoneCondition::Type::HasItem },
        { "milestoneComplete", MilestoneCondition::Type::MilestoneComplete },
        { "milestoneStarted", MilestoneCondition::Type::MilestoneStarted },
        { "sceneEntered", MilestoneCondition::Type::SceneEntered },
        { "npcDead", MilestoneCondition::Type::NpcDead },
        { "timeExpired", MilestoneCondition::Type::TimeExpired }
    };

    for (const NamedCondition& named : kNamedConditions)
    {
        if (!object.contains(named.key))
            continue;

        out.type = named.type;
        if (named.type == MilestoneCondition::Type::TimeExpired)
        {
            if (object[named.key].is_boolean())
                out.value = object[named.key].get<bool>() ? "true" : "false";
            else
                out.value = "true";
            return true;
        }

        if (!object[named.key].is_string())
            return false;

        out.value = object[named.key].get<std::string>();
        return !out.value.empty() || named.type == MilestoneCondition::Type::TimeExpired;
    }

    return false;
}

bool parseOptionalCondition(const nlohmann::json& object, const char* key, MilestoneCondition& out)
{
    if (!object.contains(key))
        return true;

    return parseConditionObject(object[key], out);
}

bool parseTimeLimit(const nlohmann::json& object, MilestoneTimeLimit& out)
{
    out = MilestoneTimeLimit{};
    if (!object.is_object())
        return true;

    out.amount = object.value("amount", 0);
    out.unit = parseTimeUnit(object.value("unit", ""));
    return true;
}

bool parseObjective(const nlohmann::json& objective, MilestoneObjectiveDef& out)
{
    if (!objective.is_object())
        return false;

    out.id = objective.value("id", "");
    out.text = objective.value("text", "");
    out.required = objective.value("required", false);
    if (!parseOptionalCondition(objective, "completeWhen", out.completeWhen))
        return false;

    return !out.id.empty() && !out.text.empty() && out.completeWhen.type != MilestoneCondition::Type::None;
}

bool parseMilestone(const std::string& id, const nlohmann::json& milestone, MilestoneDef& out)
{
    if (!milestone.is_object())
        return false;

    out = MilestoneDef{};
    out.id = id;
    out.isQuest = milestone.value("isQuest", false);
    out.title = milestone.value("title", "");
    out.summary = milestone.value("summary", "");
    out.completeSummary = milestone.value("completeSummary", "");

    const nlohmann::json& requires = milestone.value("requires", nlohmann::json::array());
    if (!requires.is_array())
        return false;

    for (const nlohmann::json& entry : requires)
    {
        if (!entry.is_string())
            return false;
        out.requires.push_back(entry.get<std::string>());
    }

    if (!parseOptionalCondition(milestone, "startWhen", out.startWhen))
        return false;
    if (!parseOptionalCondition(milestone, "completeWhen", out.completeWhen))
        return false;
    if (!parseOptionalCondition(milestone, "failWhen", out.failWhen))
        return false;
    if (!parseTimeLimit(milestone.value("timeLimit", nlohmann::json::object()), out.timeLimit))
        return false;

    const nlohmann::json& objectives = milestone.value("objectives", nlohmann::json::array());
    if (!objectives.is_array())
        return false;

    for (const nlohmann::json& objective : objectives)
    {
        MilestoneObjectiveDef parsed;
        if (!parseObjective(objective, parsed))
            return false;
        out.objectives.push_back(parsed);
    }

    if (out.completeWhen.type == MilestoneCondition::Type::None)
        return false;

    if (out.isQuest && out.title.empty())
        return false;

    return true;
}

}

bool MilestoneDatabase::load(const std::string& configPath)
{
    milestones.clear();

    std::ifstream file(configPath.c_str());
    if (!file.is_open())
    {
        TraceLog(LOG_WARNING, "Milestone config not found: %s", configPath.c_str());
        return false;
    }

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        TraceLog(LOG_ERROR, "Failed to parse milestone config: %s", configPath.c_str());
        return false;
    }

    const nlohmann::json& milestoneMap = root.contains("milestones")
        ? root["milestones"]
        : root;

    if (!milestoneMap.is_object())
    {
        TraceLog(LOG_ERROR, "Milestone config must contain a milestones object: %s", configPath.c_str());
        return false;
    }

    for (auto it = milestoneMap.begin(); it != milestoneMap.end(); ++it)
    {
        MilestoneDef parsed;
        if (!parseMilestone(it.key(), it.value(), parsed))
        {
            TraceLog(LOG_ERROR, "Failed to parse milestone '%s'", it.key().c_str());
            return false;
        }

        milestones[parsed.id] = parsed;
    }

    TraceLog(LOG_INFO, "Loaded %d milestones from %s", (int)milestones.size(), configPath.c_str());
    return !milestones.empty();
}

const MilestoneDef* MilestoneDatabase::find(const std::string& milestoneId) const
{
    std::map<std::string, MilestoneDef>::const_iterator it = milestones.find(milestoneId);
    if (it == milestones.end())
        return nullptr;
    return &it->second;
}

}