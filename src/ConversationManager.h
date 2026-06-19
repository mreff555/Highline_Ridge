#ifndef CONVERSATION_MANAGER_H
#define CONVERSATION_MANAGER_H

#include <ConversationStruct.h>
#include <map>
#include <set>
#include <string>

namespace testgame
{

class ConversationManager
{
    public:
    void onEnterScene(const std::string& sceneId, const SceneSpeakConfig& config);
    bool canSpeak(
        const SceneSpeakConfig& config,
        bool baseSpeakEnabled,
        const std::set<std::string>& storyFlags) const;
    bool isAwaitingChoice() const { return awaitingChoice; }
    bool isCombatAttackAllowed() const { return combatAttackAllowed; }
    const std::string& getCombatEncounterId() const { return combatEncounterId; }
    const std::vector<ConversationChoiceDef>& getPendingChoices() const { return pendingChoices; }
    void clearPendingEncounter();

    SpeakResult handleSpeak(
        const std::string& sceneId,
        const SceneSpeakConfig& config,
        const std::set<std::string>& storyFlags);
    SpeakResult resolveChoice(const std::string& choiceId);
    SpeakResult resolveChoiceFromConfig(const SceneSpeakConfig& config, const std::string& choiceId);

    private:
    bool isPhaseComplete(const std::string& phaseId) const;
    void markPhaseComplete(const std::string& phaseId);
    bool isPhaseRequirementMet(
        const ConversationPhase& phase,
        const std::set<std::string>& storyFlags) const;
    const ConversationPhase* findPhase(const SceneSpeakConfig& config, const std::string& phaseId) const;
    const ConversationPhase* findActivePhase(
        const std::string& sceneId,
        const SceneSpeakConfig& config,
        const std::set<std::string>& storyFlags) const;
    SpeakResult buildNarrativeResult(const std::string& text, const StatusEffect& status) const;
    SpeakResult pickRandomLine(const std::string& sceneId, const ConversationPhase& phase);
    std::string randomPoolKey(const std::string& sceneId, const ConversationPhase& phase) const;

    std::string currentSceneId;
    std::set<std::string> completedPhaseIds;
    std::map<std::string, int> lastRandomLineIndex;
    bool awaitingChoice = false;
    bool combatAttackAllowed = false;
    std::string combatEncounterId;
    std::string activeScriptPhaseId;
    std::vector<ConversationChoiceDef> pendingChoices;
};

}

#endif /* CONVERSATION_MANAGER_H */