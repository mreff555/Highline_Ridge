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
    void onEnterRoom(const std::string& roomId, const RoomSpeakConfig& config);
    bool canSpeak(
        const RoomSpeakConfig& config,
        bool baseSpeakEnabled,
        const std::set<std::string>& storyFlags) const;
    bool isAwaitingChoice() const { return awaitingChoice; }
    const std::vector<ConversationChoiceDef>& getPendingChoices() const { return pendingChoices; }

    SpeakResult handleSpeak(
        const std::string& roomId,
        const RoomSpeakConfig& config,
        const std::set<std::string>& storyFlags);
    SpeakResult resolveChoice(const std::string& choiceId);
    SpeakResult resolveChoiceFromConfig(const RoomSpeakConfig& config, const std::string& choiceId);

    private:
    bool isPhaseComplete(const std::string& phaseId) const;
    void markPhaseComplete(const std::string& phaseId);
    bool isPhaseRequirementMet(
        const ConversationPhase& phase,
        const std::set<std::string>& storyFlags) const;
    const ConversationPhase* findPhase(const RoomSpeakConfig& config, const std::string& phaseId) const;
    const ConversationPhase* findActivePhase(
        const std::string& roomId,
        const RoomSpeakConfig& config,
        const std::set<std::string>& storyFlags) const;
    SpeakResult buildNarrativeResult(const std::string& text, const StatusEffect& status) const;
    SpeakResult pickRandomLine(const std::string& roomId, const ConversationPhase& phase);
    std::string randomPoolKey(const std::string& roomId, const ConversationPhase& phase) const;

    std::string currentRoomId;
    std::set<std::string> completedPhaseIds;
    std::map<std::string, int> lastRandomLineIndex;
    bool awaitingChoice = false;
    std::string activeScriptPhaseId;
    std::vector<ConversationChoiceDef> pendingChoices;
};

}

#endif /* CONVERSATION_MANAGER_H */