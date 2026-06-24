#include "SceneOverlayMgr.h"

#include <algorithm>
#include <cmath>

namespace testgame
{

namespace
{

float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

float lerp(float from, float to, float t)
{
    return from + (to - from) * clamp01(t);
}

}

void SceneOverlayMgr::clear()
{
    activeSceneOverlays.clear();
    sequenceSteps.clear();
    sequenceIndex = 0;
    stepElapsed = 0.0f;
    sequenceFadeOpacity = 0.0f;
    sequenceVignetteOcclusion = 0.0f;
    stepStartFadeOpacity = 0.0f;
    stepStartVignetteOcclusion = 0.0f;
    sequenceFadeColor = BLACK;
    sequenceActive = false;
    sequenceCompletedPending = false;
    sequenceCompleteCallback = nullptr;
}

void SceneOverlayMgr::setSceneOverlays(const std::vector<SceneOverlayDef>& overlays)
{
    activeSceneOverlays.clear();
    activeSceneOverlays.reserve(overlays.size());

    for (const SceneOverlayDef& overlay : overlays)
    {
        ActiveSceneOverlay active;
        active.def = overlay;
        active.currentOcclusion = 0.0f;
        active.currentOpacity = 0.0f;
        active.phaseElapsed = 0.0f;

        if (overlay.type == OverlayEffectType::Fade)
        {
            if (overlay.fadeInSeconds <= 0.0f)
            {
                active.currentOpacity = overlay.opacity;
                active.phase = SceneOverlayPhase::Done;
            }
            else
            {
                active.phase = SceneOverlayPhase::FadeIn;
            }
        }
        else if (overlay.fadeInSeconds <= 0.0f && !overlay.animate.enabled)
        {
            active.currentOcclusion = overlay.occlusionPercent;
            active.phase = SceneOverlayPhase::Done;
        }
        else if (overlay.fadeInSeconds <= 0.0f && overlay.animate.enabled)
        {
            active.currentOcclusion = overlay.occlusionPercent;
            active.phase = SceneOverlayPhase::Animate;
        }
        else
        {
            active.phase = SceneOverlayPhase::FadeIn;
        }

        activeSceneOverlays.push_back(active);
    }
}

void SceneOverlayMgr::startSequence(
    const std::vector<OverlaySequenceStep>& steps,
    std::function<void()> onComplete)
{
    sequenceSteps = steps;
    sequenceIndex = 0;
    stepElapsed = 0.0f;
    stepStartFadeOpacity = sequenceFadeOpacity;
    stepStartVignetteOcclusion = sequenceVignetteOcclusion;
    sequenceActive = !sequenceSteps.empty();
    sequenceCompletedPending = false;
    sequenceCompleteCallback = std::move(onComplete);

    if (!sequenceActive && sequenceCompleteCallback)
    {
        sequenceCompleteCallback();
        sequenceCompleteCallback = nullptr;
    }
}

bool SceneOverlayMgr::consumeSequenceCompleted()
{
    const bool completed = sequenceCompletedPending;
    sequenceCompletedPending = false;
    return completed;
}

void SceneOverlayMgr::advanceSequenceStep()
{
    if (!sequenceActive)
        return;

    if (sequenceIndex >= sequenceSteps.size())
    {
        sequenceActive = false;
        sequenceCompletedPending = true;
        if (sequenceCompleteCallback)
        {
            sequenceCompleteCallback();
            sequenceCompleteCallback = nullptr;
        }
        return;
    }

    const OverlaySequenceStep& step = sequenceSteps[sequenceIndex];
    stepStartFadeOpacity = sequenceFadeOpacity;
    stepStartVignetteOcclusion = sequenceVignetteOcclusion;
    stepElapsed = 0.0f;

    if (step.action == OverlaySequenceAction::FadeTo)
        sequenceFadeColor = step.color;

    if (step.durationSeconds <= 0.0f)
    {
        if (step.action == OverlaySequenceAction::FadeTo)
            sequenceFadeOpacity = clamp01(step.targetOpacity);
        else if (step.action == OverlaySequenceAction::VignetteTo)
            sequenceVignetteOcclusion = std::max(0.0f, step.targetOcclusionPercent);

        ++sequenceIndex;
        advanceSequenceStep();
    }
}

void SceneOverlayMgr::update(float deltaSeconds)
{
    for (ActiveSceneOverlay& overlay : activeSceneOverlays)
    {
        if (overlay.phase == SceneOverlayPhase::Done)
            continue;

        overlay.phaseElapsed += deltaSeconds;

        if (overlay.def.type == OverlayEffectType::Fade)
        {
            if (overlay.phase == SceneOverlayPhase::FadeIn)
            {
                const float duration = std::max(overlay.def.fadeInSeconds, 0.0001f);
                const float t = overlay.phaseElapsed / duration;
                overlay.currentOpacity = lerp(0.0f, overlay.def.opacity, t);
                if (t >= 1.0f)
                    overlay.phase = SceneOverlayPhase::Done;
            }
            continue;
        }

        if (overlay.phase == SceneOverlayPhase::FadeIn)
        {
            const float duration = std::max(overlay.def.fadeInSeconds, 0.0001f);
            const float t = overlay.phaseElapsed / duration;
            overlay.currentOcclusion = lerp(0.0f, overlay.def.occlusionPercent, t);
            if (t >= 1.0f)
            {
                overlay.phaseElapsed = 0.0f;
                overlay.phase = overlay.def.animate.enabled
                    ? SceneOverlayPhase::Animate
                    : SceneOverlayPhase::Steady;
            }
            continue;
        }

        if (overlay.phase == SceneOverlayPhase::Animate)
        {
            const float duration = std::max(overlay.def.animate.durationSeconds, 0.0001f);
            const float t = overlay.phaseElapsed / duration;
            overlay.currentOcclusion = lerp(
                overlay.def.occlusionPercent,
                overlay.def.animate.targetOcclusionPercent,
                t);
            if (t >= 1.0f)
                overlay.phase = SceneOverlayPhase::Done;
            continue;
        }

        overlay.currentOcclusion = overlay.def.occlusionPercent;
    }

    if (!sequenceActive)
        return;

    if (sequenceIndex >= sequenceSteps.size())
        return;

    const OverlaySequenceStep& step = sequenceSteps[sequenceIndex];
    stepElapsed += deltaSeconds;

    if (step.action == OverlaySequenceAction::Hold)
    {
        if (stepElapsed >= step.durationSeconds)
        {
            ++sequenceIndex;
            advanceSequenceStep();
        }
        return;
    }

    const float duration = std::max(step.durationSeconds, 0.0001f);
    const float t = stepElapsed / duration;

    if (step.action == OverlaySequenceAction::FadeTo)
        sequenceFadeOpacity = lerp(stepStartFadeOpacity, clamp01(step.targetOpacity), t);
    else if (step.action == OverlaySequenceAction::VignetteTo)
    {
        sequenceVignetteOcclusion = lerp(
            stepStartVignetteOcclusion,
            std::max(0.0f, step.targetOcclusionPercent),
            t);
    }

    if (t >= 1.0f)
    {
        ++sequenceIndex;
        advanceSequenceStep();
    }
}

float SceneOverlayMgr::combinedVignetteOcclusion() const
{
    float maxOcclusion = sequenceVignetteOcclusion;
    for (const ActiveSceneOverlay& overlay : activeSceneOverlays)
    {
        if (overlay.def.type != OverlayEffectType::Vignette)
            continue;
        maxOcclusion = std::max(maxOcclusion, overlay.currentOcclusion);
    }
    return maxOcclusion;
}

float SceneOverlayMgr::combinedFadeOpacity(Color& outColor) const
{
    float maxOpacity = sequenceFadeOpacity;
    outColor = sequenceFadeColor;

    for (const ActiveSceneOverlay& overlay : activeSceneOverlays)
    {
        if (overlay.def.type != OverlayEffectType::Fade)
            continue;
        if (overlay.currentOpacity > maxOpacity)
        {
            maxOpacity = overlay.currentOpacity;
            outColor = overlay.def.color;
        }
    }

    return clamp01(maxOpacity);
}

void SceneOverlayMgr::drawVignette(Rectangle bounds, float occlusionPercent) const
{
    const float intensity = clamp01(occlusionPercent / 100.0f);
    if (intensity <= 0.001f)
        return;

    const float edge = std::min(bounds.width, bounds.height) * 0.45f * intensity;
    const unsigned char alpha = (unsigned char)(255.0f * intensity);
    const Color edgeColor = { 0, 0, 0, alpha };
    const Color clearColor = { 0, 0, 0, 0 };

    DrawRectangleGradientV(bounds.x, bounds.y, bounds.width, edge, edgeColor, clearColor);
    DrawRectangleGradientV(
        bounds.x,
        bounds.y + bounds.height - edge,
        bounds.width,
        edge,
        clearColor,
        edgeColor);
    DrawRectangleGradientH(bounds.x, bounds.y, edge, bounds.height, edgeColor, clearColor);
    DrawRectangleGradientH(
        bounds.x + bounds.width - edge,
        bounds.y,
        edge,
        bounds.height,
        clearColor,
        edgeColor);
}

void SceneOverlayMgr::drawFadeOverlay(Rectangle bounds, float opacity, Color color) const
{
    const float clamped = clamp01(opacity);
    if (clamped <= 0.001f)
        return;

    Color fill = color;
    fill.a = (unsigned char)(fill.a * clamped);
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, fill);
}

void SceneOverlayMgr::draw(Rectangle bounds) const
{
    const float vignette = combinedVignetteOcclusion();
    Color fadeColor = BLACK;
    const float fadeOpacity = combinedFadeOpacity(fadeColor);

    drawVignette(bounds, vignette);
    drawFadeOverlay(bounds, fadeOpacity, fadeColor);
}

}