#ifndef SCENE_INTERACTION_DEF_H
#define SCENE_INTERACTION_DEF_H

#include <SceneOverlayDef.h>
#include <string>

namespace testgame
{

struct SceneInteractionDef
{
    std::string id;
    std::string label;
    std::string useDetails;
    std::string exitSceneId;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    float useResolveDelta = 0.0f;
    float useLucidityDelta = 0.0f;
    float useCharismaDelta = 0.0f;
    bool repeat = false;
    bool requiresExamine = true;
    bool advancesDay = false;
    std::vector<OverlaySequenceStep> overlaySequence;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    std::string ttsVariantFlag;
    std::string ttsVariantText;
    std::string ttsVariantAudio;
};

}

#endif /* SCENE_INTERACTION_DEF_H */