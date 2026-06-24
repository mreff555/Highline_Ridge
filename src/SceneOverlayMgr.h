#ifndef SCENE_OVERLAY_MGR_H
#define SCENE_OVERLAY_MGR_H

#include <SceneOverlayDef.h>
#include <raylib.h>
#include <functional>
#include <vector>

namespace testgame
{

class SceneOverlayMgr
{
    public:
    void clear();
    void setSceneOverlays(const std::vector<SceneOverlayDef>& overlays);
    void startSequence(
        const std::vector<OverlaySequenceStep>& steps,
        std::function<void()> onComplete = nullptr);
    bool isSequenceActive() const { return sequenceActive; }
    bool consumeSequenceCompleted();
    void update(float deltaSeconds);
    void draw(Rectangle bounds) const;

    private:
    enum class SceneOverlayPhase
    {
        FadeIn,
        Steady,
        Animate,
        Done
    };

    struct ActiveSceneOverlay
    {
        SceneOverlayDef def;
        SceneOverlayPhase phase = SceneOverlayPhase::FadeIn;
        float currentOcclusion = 0.0f;
        float currentOpacity = 0.0f;
        float phaseElapsed = 0.0f;
    };

    void advanceSequenceStep();
    void drawVignette(Rectangle bounds, float occlusionPercent) const;
    void drawFadeOverlay(Rectangle bounds, float opacity, Color color) const;
    float combinedVignetteOcclusion() const;
    float combinedFadeOpacity(Color& outColor) const;

    std::vector<ActiveSceneOverlay> activeSceneOverlays;
    std::vector<OverlaySequenceStep> sequenceSteps;
    size_t sequenceIndex = 0;
    float stepElapsed = 0.0f;
    float sequenceFadeOpacity = 0.0f;
    float sequenceVignetteOcclusion = 0.0f;
    float stepStartFadeOpacity = 0.0f;
    float stepStartVignetteOcclusion = 0.0f;
    Color sequenceFadeColor = BLACK;
    bool sequenceActive = false;
    bool sequenceCompletedPending = false;
    std::function<void()> sequenceCompleteCallback;
};

}

#endif /* SCENE_OVERLAY_MGR_H */