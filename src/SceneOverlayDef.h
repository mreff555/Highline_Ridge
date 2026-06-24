#ifndef SCENE_OVERLAY_DEF_H
#define SCENE_OVERLAY_DEF_H

#include <nlohmann/json_fwd.hpp>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

enum class OverlayEffectType
{
    Vignette,
    Fade
};

struct OverlayAnimateDef
{
    float targetOcclusionPercent = 0.0f;
    float durationSeconds = 0.0f;
    bool enabled = false;
};

struct SceneOverlayDef
{
    std::string id;
    OverlayEffectType type = OverlayEffectType::Vignette;
    float occlusionPercent = 0.0f;
    float opacity = 0.0f;
    Color color = BLACK;
    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;
    OverlayAnimateDef animate;
};

enum class OverlaySequenceAction
{
    FadeTo,
    Hold,
    VignetteTo
};

struct OverlaySequenceStep
{
    OverlaySequenceAction action = OverlaySequenceAction::Hold;
    float targetOpacity = 0.0f;
    float targetOcclusionPercent = 0.0f;
    float durationSeconds = 0.0f;
    Color color = BLACK;
};

bool parseOverlayColor(const nlohmann::json& value, Color& outColor);
bool parseOverlayAnimate(const nlohmann::json& animate, OverlayAnimateDef& out);
bool parseSceneOverlay(const nlohmann::json& overlay, SceneOverlayDef& out);
bool parseSceneOverlays(const nlohmann::json& overlays, std::vector<SceneOverlayDef>& out);
bool parseOverlaySequenceStep(const nlohmann::json& step, OverlaySequenceStep& out);
bool parseOverlaySequence(const nlohmann::json& sequence, std::vector<OverlaySequenceStep>& out);

}

#endif /* SCENE_OVERLAY_DEF_H */