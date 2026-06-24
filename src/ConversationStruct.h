#ifndef CONVERSATION_STRUCT_H
#define CONVERSATION_STRUCT_H

#include <string>
#include <vector>

namespace testgame
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
    bool repeat = false;
    std::string onZeroLucidity;

    bool hasDelta() const
    {
        return health != 0.0f || energy != 0.0f || resolve != 0.0f || lucidity != 0.0f
            || charisma != 0.0f || money != 0.0f;
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
    bool closePhase = true;
    bool consumeOnSelect = false;
    bool persistConsumed = false;
    bool resumeTopLevel = false;
    std::string grantStoryFlag;
    std::string startPhase;
    std::vector<ConversationChoiceDef> followUpChoices;

    bool isAvailable(float walletCash) const
    {
        return requiresMoney <= 0.0f || walletCash >= requiresMoney;
    }
};

struct RandomConversationLine
{
    std::string id;
    std::string text;
    std::string sketchPath;
    std::string audio;
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
    ConversationPhaseType type = ConversationPhaseType::Once;
    std::string requiresPhaseId;
    std::string requiresFlag;
    bool resetOnSceneEnter = true;

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
};

}

#endif /* CONVERSATION_STRUCT_H */