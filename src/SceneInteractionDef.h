#ifndef SCENE_INTERACTION_DEF_H
#define SCENE_INTERACTION_DEF_H

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
    bool repeat = false;
    bool requiresExamine = true;
};

}

#endif /* SCENE_INTERACTION_DEF_H */