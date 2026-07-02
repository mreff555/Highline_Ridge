#ifndef CONVERSATION_STRUCT_H
#define CONVERSATION_STRUCT_H

#include <SceneOverlayDef.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace highline_ridge
{

struct StatusEffect
{
    std::string key;
    float health = 0.0f;
    float energy = 0.0f;
    float resolve = 0.0f;
    float lucidity = 0.0f;
    float charisma = 0.0f;
    float money = 0.0f;
    std::string actor;
    int actorOpinion = 0;
    float actorTab = 0.0f;
    bool repeat = false;
    std::string onZeroLucidity;

    bool hasPlayerDelta() const
    {
        return health != 0.0f || energy != 0.0f || resolve != 0.0f || lucidity != 0.0f
            || charisma != 0.0f || money != 0.0f;
    }

    bool hasActorOpinionDelta() const
    {
        return !actor.empty() && actorOpinion != 0;
    }

    bool hasActorTabDelta() const
    {
        return !actor.empty() && actorTab != 0.0f;
    }

    bool hasDelta() const
    {
        return hasPlayerDelta() || hasActorOpinionDelta() || hasActorTabDelta();
    }
};

struct DialogRequirementFields
{
    int minDay = 0;
    int maxDay = 0;
    float requiresLucidityBelow = 0.0f;
    float requiresLucidityAbove = 0.0f;
    bool requiresRecentCollapse = false;
    bool requiresRecentSleep = false;
    std::string requiresDaysSinceFlag;
    int requiresDaysSinceMin = 0;
    int requiresDaysSinceMax = 0;
};

struct ConversationRequirementContext
{
    int currentDay = 1;
    float lucidity = 30.0f;
    int lastLucidityCollapseDay = 0;
    int lastSleepDay = 0;
    const std::map<std::string, int>* flagGrantedDay = nullptr;
};

bool meetsDialogRequirements(
    const DialogRequirementFields& requirements,
    const ConversationRequirementContext& context);

struct ChoiceAvailabilityContext
{
    float walletCash = 0.0f;
    const std::map<std::string, float>* actorTabOwed = nullptr;
    int currentDay = 1;
    int saloonRoomPurchasedDay = 0;

    bool hasSaloonRoomTonight() const
    {
        return saloonRoomPurchasedDay > 0 && saloonRoomPurchasedDay == currentDay;
    }

    float actorTabOwedTo(const std::string& actorId) const
    {
        if (actorId.empty() || actorTabOwed == nullptr)
            return 0.0f;

        std::map<std::string, float>::const_iterator it = actorTabOwed->find(actorId);
        if (it == actorTabOwed->end())
            return 0.0f;

        return std::max(0.0f, it->second);
    }
};

struct GrantedInventoryItemDef
{
    std::string id;
    std::string name;
    std::string iconPath;
    std::string examineImagePath;
    std::string examineText;

    bool isValid() const { return !id.empty(); }
};

struct ConversationChoiceDef
{
    std::string id;
    std::string label;
    std::string response;
    std::string responseAudio;
    std::string sketchPath;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    StatusEffect status;
    GrantedInventoryItemDef grantItem;
    float requiresMoney = 0.0f;
    float requiresInsufficientMoney = 0.0f;
    std::string tabActor;
    bool requiresPositiveActorTab = false;
    bool requiresPayActorTabInFull = false;
    bool requiresInsufficientForActorTab = false;
    bool payActorTabInFull = false;
    bool requiresSaloonRoomAvailable = false;
    bool closePhase = true;
    bool consumeOnSelect = false;
    bool persistConsumed = false;
    bool resumeTopLevel = false;
    std::string resumeChoiceId;
    std::string grantStoryFlag;
    std::string startPhase;
    bool skipIntroOnStartPhase = false;
    std::vector<OverlaySequenceStep> overlaySequence;
    std::vector<ConversationChoiceDef> followUpChoices;

    bool isAvailable(const ChoiceAvailabilityContext& context) const
    {
        if (requiresMoney > 0.0f && context.walletCash < requiresMoney)
            return false;

        if (requiresInsufficientMoney > 0.0f && context.walletCash >= requiresInsufficientMoney)
            return false;

        const std::string actorId = tabActor;
        const float tabOwed = context.actorTabOwedTo(actorId);

        if (requiresPositiveActorTab && tabOwed <= 0.0f)
            return false;

        if (requiresPayActorTabInFull && (tabOwed <= 0.0f || context.walletCash < tabOwed))
            return false;

        if (requiresInsufficientForActorTab && (tabOwed <= 0.0f || context.walletCash >= tabOwed))
            return false;

        if (requiresSaloonRoomAvailable && context.hasSaloonRoomTonight())
            return false;

        return true;
    }

    bool isAvailable(float walletCash) const
    {
        ChoiceAvailabilityContext context;
        context.walletCash = walletCash;
        return isAvailable(context);
    }
};

struct SceneActorDef
{
    std::string id;
    std::string name;
};

struct RandomConversationLine
{
    std::string id;
    std::string actorId;
    std::string actorName;
    DialogRequirementFields requirements;
    std::string text;
    std::string sketchPath;
    std::string audio;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    StatusEffect status;
    int weight = 1;
    bool once = false;
    bool allowAttack = false;
    std::string attackEncounterId;
    std::vector<ConversationChoiceDef> choices;
};

enum class ConversationPhaseType
{
    Once,
    Scripted,
    Random
};

struct ConversationPhase
{
    std::string id;
    std::string actorId;
    std::string actorName;
    ConversationPhaseType type = ConversationPhaseType::Once;
    std::string requiresPhaseId;
    std::string requiresFlag;
    DialogRequirementFields requirements;
    bool resetOnSceneEnter = true;
    bool repeatable = false;

    std::string text;
    std::string audio;
    StatusEffect status;
    GrantedInventoryItemDef grantItem;

    std::string intro;
    std::string introAudio;
    std::string sketchPath;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    bool resumeTts = false;
    std::string resumeTtsVoice;
    std::string resumeTtsText;
    std::string resumeTtsAudio;
    std::string resumeIntro;
    std::string resumeIntroAudio;
    std::vector<ConversationChoiceDef> choices;

    std::string poolId;
    std::vector<RandomConversationLine> lines;
    bool avoidRepeat = true;
};

struct SceneSpeakConfig
{
    std::vector<ConversationPhase> phases;

    bool hasPhases() const { return !phases.empty(); }
};

struct SpeakResult
{
    enum class Action
    {
        None,
        ShowNarrative,
        ShowChoices,
        LucidityCollapse
    };

    Action action = Action::None;
    std::string narrative;
    std::string sketchPath;
    std::vector<ConversationChoiceDef> choices;
    std::vector<StatusEffect> statusEffects;
    GrantedInventoryItemDef grantItem;
    std::vector<std::string> dialogAudioTracks;
    bool useTts = false;
    std::string ttsText;
    std::string ttsVoice;
    std::vector<std::string> ttsAudioPaths;
    std::string grantStoryFlag;
    std::string startPhaseId;
    bool skipIntroOnStartPhase = false;
    std::vector<OverlaySequenceStep> overlaySequence;
    std::string spokenActorId;
};

}

#endif /* CONVERSATION_STRUCT_H */