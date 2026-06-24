#include <ConversationManager.h>
#include <ProgressionService.h>
#include <cstdlib>

namespace testgame
{

namespace
{

void applyTtsFields(
    SpeakResult& result,
    bool enabled,
    const std::string& ttsText,
    const std::string& ttsVoice,
    const std::string& ttsAudio,
    const std::string& fallbackNarration)
{
    if (!enabled || ttsAudio.empty())
        return;

    result.useTts = true;
    result.ttsAudioPaths.push_back(ttsAudio);
    result.ttsVoice = ttsVoice;
    result.ttsText = !ttsText.empty() ? ttsText : fallbackNarration;
}

void applyTtsAfterFields(
    SpeakResult& result,
    bool enabled,
    const std::string& ttsAfterText,
    const std::string& /*ttsAfterVoice*/,
    const std::string& ttsAfterAudio,
    const std::string& fallbackNarration)
{
    if (!enabled || ttsAfterAudio.empty())
        return;

    result.useTts = true;
    result.ttsAudioPaths.push_back(ttsAfterAudio);
    if (result.ttsText.empty())
        result.ttsText = !ttsAfterText.empty() ? ttsAfterText : fallbackNarration;
}

}

void ConversationManager::onEnterScene(const std::string& sceneId, const SceneSpeakConfig& config)
{
    currentSceneId = sceneId;

    for (const ConversationPhase& phase : config.phases)
    {
        if (phase.resetOnSceneEnter)
        {
            completedPhaseIds.erase(phase.id);
            clearConsumedScriptedChoices(phase.id);
            lastRandomLineIndex.erase(randomPoolKey(sceneId, phase));
        }
    }

    awaitingChoice = false;
    combatAttackAllowed = false;
    combatEncounterId.clear();
    activeScriptPhaseId.clear();
    activeParentChoiceId.clear();
    pendingChoices.clear();
}

void ConversationManager::clearPendingEncounter()
{
    if (!activeScriptPhaseId.empty())
        markPhaseComplete(activeScriptPhaseId);

    awaitingChoice = false;
    combatAttackAllowed = false;
    combatEncounterId.clear();
    activeScriptPhaseId.clear();
    activeParentChoiceId.clear();
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

    if (!phase.requiresFlag.empty())
    {
        const bool hasFlag = progressionService != nullptr
            ? progressionService->hasStoryFlag(storyFlags, phase.requiresFlag)
            : storyFlags.count(phase.requiresFlag) > 0;
        if (!hasFlag)
            return false;
    }

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

void ConversationManager::appendDialogAudioTrack(SpeakResult& result, const std::string& path) const
{
    if (!path.empty())
        result.dialogAudioTracks.push_back(path);
}

SpeakResult ConversationManager::buildNarrativeResult(
    const std::string& text,
    const StatusEffect& status,
    const GrantedInventoryItemDef& grantItem,
    const std::vector<std::string>& dialogAudioTracks) const
{
    SpeakResult result;
    if (text.empty() && !grantItem.isValid())
        return result;

    result.action = SpeakResult::Action::ShowNarrative;
    result.narrative = text;
    if (status.hasDelta())
        result.statusEffects.push_back(status);
    if (grantItem.isValid())
        result.grantItem = grantItem;
    result.dialogAudioTracks = dialogAudioTracks;
    return result;
}

std::string ConversationManager::scriptedChoiceKey(
    const std::string& phaseId,
    const std::string& choiceId) const
{
    return phaseId + ":" + choiceId;
}

bool ConversationManager::isScriptedChoiceConsumed(
    const std::string& phaseId,
    const std::string& choiceId) const
{
    return consumedScriptedChoiceIds.count(scriptedChoiceKey(phaseId, choiceId)) > 0;
}

void ConversationManager::markScriptedChoiceConsumed(
    const std::string& phaseId,
    const std::string& choiceId,
    bool persist)
{
    const std::string key = scriptedChoiceKey(phaseId, choiceId);
    consumedScriptedChoiceIds.insert(key);
    if (persist)
        persistedConsumedScriptedChoiceIds.insert(key);
}

void ConversationManager::clearConsumedScriptedChoices(const std::string& phaseId)
{
    const std::string prefix = phaseId + ":";
    for (auto it = consumedScriptedChoiceIds.begin(); it != consumedScriptedChoiceIds.end();)
    {
        if (it->compare(0, prefix.size(), prefix) == 0)
        {
            if (persistedConsumedScriptedChoiceIds.count(*it) > 0)
                ++it;
            else
                it = consumedScriptedChoiceIds.erase(it);
        }
        else
            ++it;
    }
}

std::vector<ConversationChoiceDef> ConversationManager::remainingTopLevelChoices(
    const ConversationPhase& phase) const
{
    std::vector<ConversationChoiceDef> remaining;
    remaining.reserve(phase.choices.size());

    for (const ConversationChoiceDef& choice : phase.choices)
    {
        if (!isScriptedChoiceConsumed(phase.id, choice.id))
            remaining.push_back(choice);
    }

    return remaining;
}

bool ConversationManager::allTopLevelChoicesConsumed(const ConversationPhase& phase) const
{
    for (const ConversationChoiceDef& choice : phase.choices)
    {
        if (!isScriptedChoiceConsumed(phase.id, choice.id))
            return false;
    }

    return true;
}

SpeakResult ConversationManager::resumeScriptedPhase(
    const ConversationPhase& phase,
    const std::string& responseText,
    const StatusEffect& status,
    const std::string& responseAudio) const
{
    const std::vector<ConversationChoiceDef> remaining = remainingTopLevelChoices(phase);
    if (remaining.empty())
    {
        std::vector<std::string> audioTracks;
        if (!responseAudio.empty())
            audioTracks.push_back(responseAudio);
        return buildNarrativeResult(responseText, status, {}, audioTracks);
    }

    SpeakResult result;
    result.action = SpeakResult::Action::ShowChoices;
    result.narrative = responseText;
    if (status.hasDelta())
        result.statusEffects.push_back(status);

    appendDialogAudioTrack(result, responseAudio);

    const std::string& resumeText = !phase.resumeIntro.empty() ? phase.resumeIntro : phase.intro;
    if (!resumeText.empty())
    {
        if (!result.narrative.empty())
            result.narrative += "\n\n";
        result.narrative += resumeText;
    }

    appendDialogAudioTrack(result, phase.resumeIntroAudio);

    result.choices = remaining;
    if (phase.resumeTts && !phase.resumeIntro.empty())
    {
        applyTtsFields(
            result,
            true,
            phase.resumeTtsText,
            phase.resumeTtsVoice,
            phase.resumeTtsAudio,
            phase.resumeIntro);
    }

    return result;
}

const ConversationChoiceDef* ConversationManager::findChoiceInList(
    const std::vector<ConversationChoiceDef>& choices,
    const std::string& choiceId) const
{
    for (const ConversationChoiceDef& choice : choices)
    {
        if (choice.id == choiceId)
            return &choice;
    }

    return nullptr;
}

const ConversationChoiceDef* ConversationManager::findChoiceInTree(
    const std::vector<ConversationChoiceDef>& choices,
    const std::string& choiceId) const
{
    for (const ConversationChoiceDef& choice : choices)
    {
        if (choice.id == choiceId)
            return &choice;

        if (!choice.followUpChoices.empty())
        {
            if (const ConversationChoiceDef* nested = findChoiceInTree(choice.followUpChoices, choiceId))
                return nested;
        }
    }

    return nullptr;
}

const ConversationChoiceDef* ConversationManager::findChoiceInPhase(
    const ConversationPhase& phase,
    const std::string& choiceId,
    const ConversationChoiceDef** outTopLevelParent) const
{
    for (const ConversationChoiceDef& topLevel : phase.choices)
    {
        if (topLevel.id == choiceId)
        {
            if (outTopLevelParent != nullptr)
                *outTopLevelParent = &topLevel;
            return &topLevel;
        }

        if (const ConversationChoiceDef* nested = findChoiceInTree(topLevel.followUpChoices, choiceId))
        {
            if (outTopLevelParent != nullptr)
                *outTopLevelParent = &topLevel;
            return nested;
        }
    }

    return nullptr;
}

const ConversationChoiceDef* ConversationManager::findTopLevelChoiceForId(
    const ConversationPhase& phase,
    const std::string& choiceId) const
{
    const ConversationChoiceDef* topLevelParent = nullptr;
    const ConversationChoiceDef* found = findChoiceInPhase(phase, choiceId, &topLevelParent);
    if (found == nullptr)
        return nullptr;

    return topLevelParent;
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

    std::vector<size_t> eligibleIndices;
    eligibleIndices.reserve(phase.lines.size());
    for (size_t i = 0; i < phase.lines.size(); ++i)
    {
        const RandomConversationLine& line = phase.lines[i];
        if (line.once && !line.id.empty() && completedRandomLineIds.count(line.id) > 0)
            continue;

        eligibleIndices.push_back(i);
    }

    if (eligibleIndices.empty())
        return SpeakResult();

    int totalWeight = 0;
    for (size_t index : eligibleIndices)
        totalWeight += phase.lines[index].weight > 0 ? phase.lines[index].weight : 1;

    if (totalWeight <= 0)
        return SpeakResult();

    const std::string poolKey = randomPoolKey(sceneId, phase);
    int chosenIndex = 0;

    if (phase.avoidRepeat && eligibleIndices.size() > 1)
    {
        const int lastIndex = lastRandomLineIndex.count(poolKey) > 0
            ? lastRandomLineIndex.at(poolKey)
            : -1;

        int guard = 0;
        do
        {
            int roll = rand() % totalWeight;
            int cumulative = 0;
            for (size_t index : eligibleIndices)
            {
                cumulative += phase.lines[index].weight > 0 ? phase.lines[index].weight : 1;
                if (roll < cumulative)
                {
                    chosenIndex = (int)index;
                    break;
                }
            }
            ++guard;
        }
        while (chosenIndex == lastIndex && guard < 12);

        lastRandomLineIndex[poolKey] = chosenIndex;
    }
    else
    {
        int roll = rand() % totalWeight;
        int cumulative = 0;
        for (size_t index : eligibleIndices)
        {
            cumulative += phase.lines[index].weight > 0 ? phase.lines[index].weight : 1;
            if (roll < cumulative)
            {
                chosenIndex = (int)index;
                break;
            }
        }
        lastRandomLineIndex[poolKey] = chosenIndex;
    }

    const RandomConversationLine& line = phase.lines[(size_t)chosenIndex];

    if (line.once && !line.id.empty())
        completedRandomLineIds.insert(line.id);

    if (!line.choices.empty())
    {
        SpeakResult result;
        result.action = SpeakResult::Action::ShowChoices;
        result.narrative = line.text;
        result.sketchPath = line.sketchPath;
        result.choices = line.choices;
        appendDialogAudioTrack(result, line.audio);
        applyTtsFields(
            result,
            line.tts,
            line.ttsText,
            line.ttsVoice,
            line.ttsAudio,
            line.text);
        applyTtsAfterFields(
            result,
            line.ttsAfter,
            line.ttsAfterText,
            line.ttsAfterVoice,
            line.ttsAfterAudio,
            line.text);
        awaitingChoice = true;
        activeScriptPhaseId = phase.id;
        pendingChoices = line.choices;
        combatAttackAllowed = line.allowAttack;
        combatEncounterId = line.attackEncounterId;
        return result;
    }

    std::vector<std::string> audioTracks;
    if (!line.audio.empty())
        audioTracks.push_back(line.audio);
    SpeakResult result = buildNarrativeResult(line.text, line.status, {}, audioTracks);
    applyTtsFields(
        result,
        line.tts,
        line.ttsText,
        line.ttsVoice,
        line.ttsAudio,
        line.text);
    applyTtsAfterFields(
        result,
        line.ttsAfter,
        line.ttsAfterText,
        line.ttsAfterVoice,
        line.ttsAfterAudio,
        line.text);
    return result;
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
        std::vector<std::string> audioTracks;
        if (!phase->audio.empty())
            audioTracks.push_back(phase->audio);
        SpeakResult result = buildNarrativeResult(phase->text, phase->status, phase->grantItem, audioTracks);
        applyTtsFields(
            result,
            phase->tts,
            phase->ttsText,
            phase->ttsVoice,
            phase->ttsAudio,
            phase->text);
        applyTtsAfterFields(
            result,
            phase->ttsAfter,
            phase->ttsAfterText,
            phase->ttsAfterVoice,
            phase->ttsAfterAudio,
            "");
        return result;
    }

    if (phase->type == ConversationPhaseType::Scripted)
        return startScriptedPhase(config, phase->id, storyFlags);

    if (phase->type == ConversationPhaseType::Random)
        return pickRandomLine(sceneId, *phase);

    return SpeakResult();
}

SpeakResult ConversationManager::startScriptedPhase(
    const SceneSpeakConfig& config,
    const std::string& phaseId,
    const std::set<std::string>& storyFlags)
{
    if (awaitingChoice)
        return SpeakResult();

    const ConversationPhase* phase = findPhase(config, phaseId);
    if (phase == nullptr || phase->type != ConversationPhaseType::Scripted)
        return SpeakResult();

    if (!isPhaseRequirementMet(*phase, storyFlags) || isPhaseComplete(phaseId))
        return SpeakResult();

    const std::vector<ConversationChoiceDef> available = remainingTopLevelChoices(*phase);
    if (available.empty())
    {
        markPhaseComplete(phaseId);
        return SpeakResult();
    }

    SpeakResult result;
    result.action = SpeakResult::Action::ShowChoices;
    result.narrative = phase->intro;
    result.sketchPath = phase->sketchPath;
    result.choices = available;
    applyTtsFields(
        result,
        phase->tts,
        phase->ttsText,
        phase->ttsVoice,
        phase->ttsAudio,
        phase->intro);
    appendDialogAudioTrack(result, phase->introAudio);
    awaitingChoice = true;
    activeScriptPhaseId = phase->id;
    pendingChoices = available;
    return result;
}

SpeakResult ConversationManager::resolveScriptedChoice(
    const SceneSpeakConfig& /*config*/,
    const ConversationPhase& phase,
    const ConversationChoiceDef& choice,
    bool fromTopLevel)
{
    if (choice.resumeTopLevel)
    {
        if (fromTopLevel && choice.consumeOnSelect)
            markScriptedChoiceConsumed(phase.id, choice.id, choice.persistConsumed);

        SpeakResult result = resumeScriptedPhase(
            phase,
            choice.response,
            choice.status,
            choice.responseAudio);
        applyTtsFields(
            result,
            choice.tts,
            choice.ttsText,
            choice.ttsVoice,
            choice.ttsAudio,
            choice.response);
        applyTtsAfterFields(
            result,
            choice.ttsAfter,
            choice.ttsAfterText,
            choice.ttsAfterVoice,
            choice.ttsAfterAudio,
            "");

        if (result.action == SpeakResult::Action::ShowChoices)
        {
            awaitingChoice = true;
            activeScriptPhaseId = phase.id;
            pendingChoices = result.choices;
        }

        return result;
    }

    if (!choice.followUpChoices.empty())
    {
        const std::vector<ConversationChoiceDef> nextChoices = choice.followUpChoices;
        const std::string responseText = choice.response;
        const std::string responseAudio = choice.responseAudio;

        activeParentChoiceId = choice.id;

        SpeakResult result;
        result.action = SpeakResult::Action::ShowChoices;
        result.narrative = responseText;
        result.choices = nextChoices;
        if (choice.status.hasDelta())
            result.statusEffects.push_back(choice.status);
        if (choice.grantItem.isValid())
            result.grantItem = choice.grantItem;
        appendDialogAudioTrack(result, responseAudio);
        applyTtsFields(
            result,
            choice.tts,
            choice.ttsText,
            choice.ttsVoice,
            choice.ttsAudio,
            choice.response);
        applyTtsAfterFields(
            result,
            choice.ttsAfter,
            choice.ttsAfterText,
            choice.ttsAfterVoice,
            choice.ttsAfterAudio,
            "");
        awaitingChoice = true;
        activeScriptPhaseId = phase.id;
        pendingChoices = nextChoices;
        return result;
    }

    const std::string consumedChoiceId = fromTopLevel ? choice.id : activeParentChoiceId;
    if (!consumedChoiceId.empty())
    {
        const ConversationChoiceDef* topLevelChoice = findTopLevelChoiceForId(phase, consumedChoiceId);
        const bool persist = topLevelChoice != nullptr && topLevelChoice->persistConsumed;
        markScriptedChoiceConsumed(phase.id, consumedChoiceId, persist);

        if (topLevelChoice != nullptr && consumedChoiceId != topLevelChoice->id)
            markScriptedChoiceConsumed(phase.id, topLevelChoice->id, topLevelChoice->persistConsumed);
    }

    activeParentChoiceId.clear();
    pendingChoices.clear();
    combatAttackAllowed = false;
    combatEncounterId.clear();
    awaitingChoice = false;
    activeScriptPhaseId.clear();
    if (choice.closePhase)
        markPhaseComplete(phase.id);

    std::vector<std::string> audioTracks;
    if (!choice.responseAudio.empty())
        audioTracks.push_back(choice.responseAudio);
    SpeakResult result = buildNarrativeResult(choice.response, choice.status, choice.grantItem, audioTracks);
    result.grantStoryFlag = choice.grantStoryFlag;
    result.startPhaseId = choice.startPhase;
    result.overlaySequence = choice.overlaySequence;
    applyTtsFields(
        result,
        choice.tts,
        choice.ttsText,
        choice.ttsVoice,
        choice.ttsAudio,
        choice.response);
    applyTtsAfterFields(
        result,
        choice.ttsAfter,
        choice.ttsAfterText,
        choice.ttsAfterVoice,
        choice.ttsAfterAudio,
        "");
    if (!choice.status.onZeroLucidity.empty())
        result.action = SpeakResult::Action::ShowNarrative;
    return result;
}

SpeakResult ConversationManager::resolveChoice(
    const SceneSpeakConfig& config,
    const std::string& choiceId)
{
    if (!awaitingChoice || activeScriptPhaseId.empty())
        return SpeakResult();

    const ConversationPhase* phase = findPhase(config, activeScriptPhaseId);
    if (!phase)
        return SpeakResult();

    const ConversationChoiceDef* chosen = findChoiceInList(pendingChoices, choiceId);
    if (!chosen)
        return SpeakResult();

    if (phase->type == ConversationPhaseType::Random)
    {
        const ConversationChoiceDef chosenCopy = *chosen;
        awaitingChoice = false;
        activeScriptPhaseId.clear();
        activeParentChoiceId.clear();
        pendingChoices.clear();
        combatAttackAllowed = false;
        combatEncounterId.clear();

        std::vector<std::string> audioTracks;
        if (!chosenCopy.responseAudio.empty())
            audioTracks.push_back(chosenCopy.responseAudio);
        return buildNarrativeResult(chosenCopy.response, chosenCopy.status, {}, audioTracks);
    }

    if (phase->type != ConversationPhaseType::Scripted)
        return SpeakResult();

    const bool fromTopLevel = activeParentChoiceId.empty();
    const ConversationChoiceDef chosenCopy = *chosen;
    return resolveScriptedChoice(config, *phase, chosenCopy, fromTopLevel);
}

SpeakResult ConversationManager::resolveChoiceFromConfig(
    const SceneSpeakConfig& config,
    const std::string& choiceId)
{
    for (const ConversationPhase& phase : config.phases)
    {
        if (phase.type != ConversationPhaseType::Scripted)
            continue;

        const ConversationChoiceDef* topLevelParent = nullptr;
        const ConversationChoiceDef* chosen = findChoiceInPhase(phase, choiceId, &topLevelParent);
        if (!chosen || !topLevelParent)
            continue;

        const bool fromTopLevel = chosen == topLevelParent;
        const ConversationChoiceDef chosenCopy = *chosen;
        return resolveScriptedChoice(config, phase, chosenCopy, fromTopLevel);
    }

    return SpeakResult();
}

void ConversationManager::exportPersistState(ConversationPersistState& out) const
{
    out.completedPhaseIds = completedPhaseIds;
    out.completedRandomLineIds = completedRandomLineIds;
    out.consumedScriptedChoiceIds = consumedScriptedChoiceIds;
    out.persistedConsumedScriptedChoiceIds = persistedConsumedScriptedChoiceIds;
}

void ConversationManager::importPersistState(const ConversationPersistState& state)
{
    completedPhaseIds = state.completedPhaseIds;
    completedRandomLineIds = state.completedRandomLineIds;
    consumedScriptedChoiceIds = state.consumedScriptedChoiceIds;
    persistedConsumedScriptedChoiceIds = state.persistedConsumedScriptedChoiceIds;
    awaitingChoice = false;
    combatAttackAllowed = false;
    combatEncounterId.clear();
    activeScriptPhaseId.clear();
    activeParentChoiceId.clear();
    pendingChoices.clear();
}

}