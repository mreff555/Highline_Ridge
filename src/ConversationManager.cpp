#include <ConversationManager.h>
#include <cstdlib>

namespace testgame
{

void ConversationManager::onEnterScene(const std::string& sceneId, const SceneSpeakConfig& config)
{
    currentSceneId = sceneId;

    for (const ConversationPhase& phase : config.phases)
    {
        if (phase.resetOnSceneEnter)
        {
            completedPhaseIds.erase(phase.id);
            lastRandomLineIndex.erase(randomPoolKey(sceneId, phase));
        }
    }

    awaitingChoice = false;
    activeScriptPhaseId.clear();
    pendingChoices.clear();
}

bool ConversationManager::isPhaseComplete(const std::string& phaseId) const
{
    return completedPhaseIds.count(phaseId) > 0;
}

void ConversationManager::markPhaseComplete(const std::string& phaseId)
{
    completedPhaseIds.insert(phaseId);
}

bool ConversationManager::isPhaseRequirementMet(
    const ConversationPhase& phase,
    const std::set<std::string>& storyFlags) const
{
    if (!phase.requiresPhaseId.empty() && !isPhaseComplete(phase.requiresPhaseId))
        return false;

    if (!phase.requiresFlag.empty() && storyFlags.count(phase.requiresFlag) == 0)
        return false;

    return true;
}

const ConversationPhase* ConversationManager::findPhase(
    const SceneSpeakConfig& config,
    const std::string& phaseId) const
{
    for (const ConversationPhase& phase : config.phases)
    {
        if (phase.id == phaseId)
            return &phase;
    }

    return nullptr;
}

const ConversationPhase* ConversationManager::findActivePhase(
    const std::string& sceneId,
    const SceneSpeakConfig& config,
    const std::set<std::string>& storyFlags) const
{
    (void)sceneId;

    for (const ConversationPhase& phase : config.phases)
    {
        if (!isPhaseRequirementMet(phase, storyFlags))
            continue;

        if (phase.type == ConversationPhaseType::Once && !isPhaseComplete(phase.id))
            return &phase;

        if (phase.type == ConversationPhaseType::Scripted && !isPhaseComplete(phase.id))
            return &phase;

        if (phase.type == ConversationPhaseType::Random && !phase.lines.empty())
            return &phase;
    }

    return nullptr;
}

bool ConversationManager::canSpeak(
    const SceneSpeakConfig& config,
    bool baseSpeakEnabled,
    const std::set<std::string>& storyFlags) const
{
    if (!baseSpeakEnabled || config.phases.empty() || awaitingChoice)
        return false;

    return findActivePhase(currentSceneId, config, storyFlags) != nullptr;
}

SpeakResult ConversationManager::buildNarrativeResult(
    const std::string& text,
    const StatusEffect& status) const
{
    SpeakResult result;
    if (text.empty())
        return result;

    result.action = SpeakResult::Action::ShowNarrative;
    result.narrative = text;
    if (status.hasDelta())
        result.statusEffects.push_back(status);
    return result;
}

std::string ConversationManager::randomPoolKey(
    const std::string& sceneId,
    const ConversationPhase& phase) const
{
    return sceneId + ":" + (phase.poolId.empty() ? phase.id : phase.poolId);
}

SpeakResult ConversationManager::pickRandomLine(
    const std::string& sceneId,
    const ConversationPhase& phase)
{
    if (phase.lines.empty())
        return SpeakResult();

    int totalWeight = 0;
    for (const RandomConversationLine& line : phase.lines)
        totalWeight += line.weight > 0 ? line.weight : 1;

    if (totalWeight <= 0)
        return SpeakResult();

    const std::string poolKey = randomPoolKey(sceneId, phase);
    int chosenIndex = 0;

    if (phase.avoidRepeat && phase.lines.size() > 1)
    {
        const int lastIndex = lastRandomLineIndex.count(poolKey) > 0
            ? lastRandomLineIndex.at(poolKey)
            : -1;

        int guard = 0;
        do
        {
            int roll = rand() % totalWeight;
            int cumulative = 0;
            for (size_t i = 0; i < phase.lines.size(); ++i)
            {
                cumulative += phase.lines[i].weight > 0 ? phase.lines[i].weight : 1;
                if (roll < cumulative)
                {
                    chosenIndex = (int)i;
                    break;
                }
            }
            ++guard;
        }
        while ((int)chosenIndex == lastIndex && guard < 12);

        lastRandomLineIndex[poolKey] = chosenIndex;
    }
    else
    {
        int roll = rand() % totalWeight;
        int cumulative = 0;
        for (size_t i = 0; i < phase.lines.size(); ++i)
        {
            cumulative += phase.lines[i].weight > 0 ? phase.lines[i].weight : 1;
            if (roll < cumulative)
            {
                chosenIndex = (int)i;
                break;
            }
        }
        lastRandomLineIndex[poolKey] = chosenIndex;
    }

    const RandomConversationLine& line = phase.lines[(size_t)chosenIndex];
    return buildNarrativeResult(line.text, line.status);
}

SpeakResult ConversationManager::handleSpeak(
    const std::string& sceneId,
    const SceneSpeakConfig& config,
    const std::set<std::string>& storyFlags)
{
    if (awaitingChoice || config.phases.empty())
        return SpeakResult();

    const ConversationPhase* phase = findActivePhase(sceneId, config, storyFlags);
    if (!phase)
        return SpeakResult();

    if (phase->type == ConversationPhaseType::Once)
    {
        markPhaseComplete(phase->id);
        return buildNarrativeResult(phase->text, phase->status);
    }

    if (phase->type == ConversationPhaseType::Scripted)
    {
        SpeakResult result;
        result.action = SpeakResult::Action::ShowChoices;
        result.narrative = phase->intro;
        result.choices = phase->choices;
        awaitingChoice = true;
        activeScriptPhaseId = phase->id;
        pendingChoices = phase->choices;
        return result;
    }

    if (phase->type == ConversationPhaseType::Random)
        return pickRandomLine(sceneId, *phase);

    return SpeakResult();
}

SpeakResult ConversationManager::resolveChoice(const std::string& choiceId)
{
    if (!awaitingChoice)
        return SpeakResult();

    const ConversationChoiceDef* chosen = nullptr;
    for (const ConversationChoiceDef& choice : pendingChoices)
    {
        if (choice.id == choiceId)
        {
            chosen = &choice;
            break;
        }
    }

    if (!chosen)
        return SpeakResult();

    const ConversationChoiceDef resolved = *chosen;

    awaitingChoice = false;
    pendingChoices.clear();

    if (!activeScriptPhaseId.empty())
        markPhaseComplete(activeScriptPhaseId);

    activeScriptPhaseId.clear();

    SpeakResult result = buildNarrativeResult(resolved.response, resolved.status);
    if (!resolved.status.onZeroLucidity.empty())
        result.action = SpeakResult::Action::ShowNarrative;
    return result;
}

SpeakResult ConversationManager::resolveChoiceFromConfig(
    const SceneSpeakConfig& config,
    const std::string& choiceId)
{
    for (const ConversationPhase& phase : config.phases)
    {
        if (phase.type != ConversationPhaseType::Scripted)
            continue;

        for (const ConversationChoiceDef& choice : phase.choices)
        {
            if (choice.id != choiceId)
                continue;

            awaitingChoice = false;
            pendingChoices.clear();
            activeScriptPhaseId.clear();
            markPhaseComplete(phase.id);
            SpeakResult result = buildNarrativeResult(choice.response, choice.status);
            if (!choice.status.onZeroLucidity.empty())
                result.action = SpeakResult::Action::ShowNarrative;
            return result;
        }
    }

    return SpeakResult();
}

}