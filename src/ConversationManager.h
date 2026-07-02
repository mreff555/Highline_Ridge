#ifndef CONVERSATION_MANAGER_H
#define CONVERSATION_MANAGER_H

#include <ConversationStruct.h>
#include <SaveGame.h>
#include <map>
#include <set>
#include <string>

namespace highline_ridge
{

class ProgressionService;

class ConversationManager
{
    public:
    void setProgressionService(const ProgressionService* service) { progressionService = service; }
    void setRequirementContext(const ConversationRequirementContext& context)
    {
        requirementContext = context;
    }
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
    SpeakResult handleSpeakTarget(
        const std::string& sceneId,
        const SceneSpeakConfig& config,
        const std::set<std::string>& storyFlags,
        const std::string& phaseId,
        const std::string& randomLineId = "");
    SpeakResult handleSpeakWorkTheRoom(
        const std::string& sceneId,
        const SceneSpeakConfig& config,
        const std::set<std::string>& storyFlags);
    bool canStartPhase(
        const ConversationPhase& phase,
        const std::set<std::string>& storyFlags) const;
    bool canPickRandomLine(const RandomConversationLine& line) const;
    bool meetsLineRequirements(const RandomConversationLine& line) const;
    bool canWorkTheRoom(
        const SceneSpeakConfig& config,
        const std::set<std::string>& storyFlags) const;
    bool isPhaseRequirementMet(
        const ConversationPhase& phase,
        const std::set<std::string>& storyFlags) const;
    std::string phaseActorId(const ConversationPhase& phase) const;
    std::string lineActorId(const RandomConversationLine& line) const;
    std::string bestStartablePhaseIdForActor(
        const SceneSpeakConfig& config,
        const std::string& actorId,
        const std::set<std::string>& storyFlags) const;
    SpeakResult startScriptedPhase(
        const SceneSpeakConfig& config,
        const std::string& phaseId,
        const std::set<std::string>& storyFlags,
        bool skipIntro = false);
    SpeakResult resolveChoice(const SceneSpeakConfig& config, const std::string& choiceId);
    SpeakResult resolveChoiceFromConfig(const SceneSpeakConfig& config, const std::string& choiceId);
    void exportPersistState(ConversationPersistState& out) const;
    void importPersistState(const ConversationPersistState& state);
    bool isPhaseComplete(const std::string& phaseId) const;

    private:
    int phaseStartPriority(const ConversationPhase& phase, size_t phaseIndex) const;
    void markPhaseComplete(const std::string& phaseId);
    void resetRepeatablePhase(const std::string& phaseId);
    const ConversationPhase* findPhase(const SceneSpeakConfig& config, const std::string& phaseId) const;
    const ConversationPhase* findActivePhase(
        const std::string& sceneId,
        const SceneSpeakConfig& config,
        const std::set<std::string>& storyFlags) const;
    SpeakResult buildNarrativeResult(
        const std::string& text,
        const StatusEffect& status,
        const GrantedInventoryItemDef& grantItem = {},
        const std::vector<std::string>& dialogAudioTracks = {}) const;
    void appendDialogAudioTrack(SpeakResult& result, const std::string& path) const;
    SpeakResult pickRandomLine(const std::string& sceneId, const ConversationPhase& phase);
    SpeakResult pickSpecificRandomLine(
        const std::string& sceneId,
        const ConversationPhase& phase,
        const std::string& lineId);
    std::string randomPoolKey(const std::string& sceneId, const ConversationPhase& phase) const;
    std::string scriptedChoiceKey(const std::string& phaseId, const std::string& choiceId) const;
    bool isScriptedChoiceConsumed(const std::string& phaseId, const std::string& choiceId) const;
    void markScriptedChoiceConsumed(
        const std::string& phaseId,
        const std::string& choiceId,
        bool persist = false);
    void clearConsumedScriptedChoices(const std::string& phaseId);
    std::vector<ConversationChoiceDef> remainingTopLevelChoices(const ConversationPhase& phase) const;
    bool allTopLevelChoicesConsumed(const ConversationPhase& phase) const;
    SpeakResult resumeScriptedPhase(
        const ConversationPhase& phase,
        const std::string& responseText,
        const StatusEffect& status,
        const std::string& responseAudio = "") const;
    const ConversationChoiceDef* findChoiceInList(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& choiceId) const;
    const ConversationChoiceDef* findChoiceInTree(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& choiceId) const;
    const ConversationChoiceDef* findChoiceInPhase(
        const ConversationPhase& phase,
        const std::string& choiceId,
        const ConversationChoiceDef** outTopLevelParent = nullptr) const;
    const ConversationChoiceDef* findTopLevelChoiceForId(
        const ConversationPhase& phase,
        const std::string& choiceId) const;
    SpeakResult resolveScriptedChoice(
        const SceneSpeakConfig& config,
        const ConversationPhase& phase,
        const ConversationChoiceDef& choice,
        bool fromTopLevel);

    std::string currentSceneId;
    std::set<std::string> completedPhaseIds;
    std::set<std::string> completedRandomLineIds;
    std::set<std::string> consumedScriptedChoiceIds;
    std::set<std::string> persistedConsumedScriptedChoiceIds;
    std::map<std::string, int> lastRandomLineIndex;
    bool awaitingChoice = false;
    bool combatAttackAllowed = false;
    std::string combatEncounterId;
    std::string activeScriptPhaseId;
    std::string activeParentChoiceId;
    std::vector<ConversationChoiceDef> pendingChoices;
    const ProgressionService* progressionService = nullptr;
    ConversationRequirementContext requirementContext;
};

}

#endif /* CONVERSATION_MANAGER_H */