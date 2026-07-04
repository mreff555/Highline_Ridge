#include <ConversationStruct.h>

namespace highline_ridge
{

bool meetsDialogRequirements(
    const DialogRequirementFields& requirements,
    const ConversationRequirementContext& context)
{
    if (requirements.minDay > 0 && context.currentDay < requirements.minDay)
        return false;

    if (requirements.maxDay > 0 && context.currentDay > requirements.maxDay)
        return false;

    if (requirements.requiresLucidityBelow > 0.0f
        && context.lucidity >= requirements.requiresLucidityBelow)
        return false;

    if (requirements.requiresLucidityAbove > 0.0f
        && context.lucidity <= requirements.requiresLucidityAbove)
        return false;

    if (requirements.requiresRecentCollapse
        && context.lastLucidityCollapseDay != context.currentDay)
        return false;

    if (requirements.requiresRecentSleep
        && context.lastSleepDay != context.currentDay)
        return false;

    if (!requirements.requiresDaysSinceFlag.empty())
    {
        if (context.flagGrantedDay == nullptr)
            return false;

        std::map<std::string, int>::const_iterator it =
            context.flagGrantedDay->find(requirements.requiresDaysSinceFlag);
        if (it == context.flagGrantedDay->end())
            return false;

        const int daysSince = context.currentDay - it->second;
        if (daysSince < requirements.requiresDaysSinceMin)
            return false;

        if (requirements.requiresDaysSinceMax > 0
            && daysSince > requirements.requiresDaysSinceMax)
            return false;
    }

    if (!requirements.opinionActor.empty()
        && (requirements.hasActorOpinionAtLeast || requirements.hasActorOpinionAtMost))
    {
        if (context.actorOpinions == nullptr)
            return false;

        int opinion = 0;
        std::map<std::string, int>::const_iterator it =
            context.actorOpinions->find(requirements.opinionActor);
        if (it != context.actorOpinions->end())
            opinion = it->second;

        if (requirements.hasActorOpinionAtLeast
            && opinion < requirements.requiresActorOpinionAtLeast)
            return false;

        if (requirements.hasActorOpinionAtMost
            && opinion > requirements.requiresActorOpinionAtMost)
            return false;
    }

    return true;
}

}