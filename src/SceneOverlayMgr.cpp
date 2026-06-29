#include "SceneOverlayMgr.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace highline_ridge
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
    sequenceHypoxiaOpacity = 0.0f;
    stepStartFadeOpacity = 0.0f;
    stepStartVignetteOcclusion = 0.0f;
    stepStartHypoxiaOpacity = 0.0f;
    sequenceFadeColor = BLACK;
    hypoxiaDots.clear();
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

void SceneOverlayMgr::setHypoxiaOpacity(float opacity)
{
    sequenceHypoxiaOpacity = clamp01(opacity);
}

void SceneOverlayMgr::setOnSequenceStepStart(
    std::function<void(const OverlaySequenceStep&, float fromOpacity, float toOpacity)> callback)
{
    onSequenceStepStart = std::move(callback);
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
    stepStartHypoxiaOpacity = sequenceHypoxiaOpacity;
    sequenceActive = !sequenceSteps.empty();
    sequenceCompletedPending = false;
    sequenceCompleteCallback = std::move(onComplete);

    if (!sequenceActive && sequenceCompleteCallback)
    {
        sequenceCompleteCallback();
        sequenceCompleteCallback = nullptr;
        return;
    }

    beginSequenceStep();
}

void SceneOverlayMgr::generateHypoxiaDots(const OverlaySequenceStep& step)
{
    hypoxiaDots.clear();

    const float peakOpacity = std::max(stepStartHypoxiaOpacity, clamp01(step.targetOpacity));
    const int dotCount = std::max(
        40,
        (int)std::lround(80.0f + 220.0f * peakOpacity));

    std::mt19937 rng((unsigned int)(sequenceIndex * 0x9E3779B1u + 0x51ED270Bu));
    std::uniform_real_distribution<float> positionDist(0.04f, 0.96f);
    std::uniform_real_distribution<float> radiusDist(2.0f, 14.0f);
    std::uniform_real_distribution<float> staggerDist(0.0f, 0.85f);
    std::uniform_int_distribution<int> colorDist(0, 2);

    hypoxiaDots.reserve((size_t)dotCount);
    for (int dotIndex = 0; dotIndex < dotCount; ++dotIndex)
    {
        HypoxiaDot dot;
        dot.nx = positionDist(rng);
        dot.ny = positionDist(rng);
        dot.radius = radiusDist(rng);
        dot.crimson = colorDist(rng) > 0;
        dot.stagger = staggerDist(rng);
        hypoxiaDots.push_back(dot);
    }
}

void SceneOverlayMgr::beginSequenceStep()
{
    if (!sequenceActive || sequenceIndex >= sequenceSteps.size())
        return;

    const OverlaySequenceStep& step = sequenceSteps[sequenceIndex];
    stepStartFadeOpacity = sequenceFadeOpacity;
    stepStartVignetteOcclusion = sequenceVignetteOcclusion;
    stepStartHypoxiaOpacity = sequenceHypoxiaOpacity;
    stepElapsed = 0.0f;

    if (step.action == OverlaySequenceAction::FadeTo)
        sequenceFadeColor = step.color;
    else if (step.action == OverlaySequenceAction::HypoxiaTo)
        generateHypoxiaDots(step);

    if (onSequenceStepStart)
    {
        if (step.action == OverlaySequenceAction::HypoxiaTo)
        {
            onSequenceStepStart(
                step,
                stepStartHypoxiaOpacity,
                clamp01(step.targetOpacity));
        }
        else if (step.action == OverlaySequenceAction::FadeTo)
        {
            onSequenceStepStart(
                step,
                stepStartFadeOpacity,
                clamp01(step.targetOpacity));
        }
    }

    if (step.durationSeconds <= 0.0f)
    {
        if (step.action == OverlaySequenceAction::FadeTo)
            sequenceFadeOpacity = clamp01(step.targetOpacity);
        else if (step.action == OverlaySequenceAction::VignetteTo)
            sequenceVignetteOcclusion = std::max(0.0f, step.targetOcclusionPercent);
        else if (step.action == OverlaySequenceAction::HypoxiaTo)
            sequenceHypoxiaOpacity = clamp01(step.targetOpacity);

        ++sequenceIndex;
        beginSequenceStep();
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

    ++sequenceIndex;

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

    beginSequenceStep();
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
            advanceSequenceStep();
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
    else if (step.action == OverlaySequenceAction::HypoxiaTo)
    {
        sequenceHypoxiaOpacity = lerp(stepStartHypoxiaOpacity, clamp01(step.targetOpacity), t);
        if (t >= 1.0f && clamp01(step.targetOpacity) <= 0.001f)
            hypoxiaDots.clear();
    }

    if (t >= 1.0f)
        advanceSequenceStep();
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

void SceneOverlayMgr::drawHypoxiaOverlay(Rectangle bounds) const
{
    const float hypoxia = clamp01(sequenceHypoxiaOpacity);
    if (hypoxia <= 0.001f && hypoxiaDots.empty())
        return;

    for (const HypoxiaDot& dot : hypoxiaDots)
    {
        const float reveal = clamp01((hypoxia - dot.stagger * 0.35f) / 0.65f);
        if (reveal <= 0.001f)
            continue;

        const float alpha = reveal * (0.35f + 0.65f * hypoxia);
        Color fill = dot.crimson ? Color{ 72, 10, 20, 255 } : Color{ 0, 0, 0, 255 };
        fill.a = (unsigned char)(fill.a * alpha);
        const float centerX = bounds.x + dot.nx * bounds.width;
        const float centerY = bounds.y + dot.ny * bounds.height;
        DrawCircleV({ centerX, centerY }, dot.radius, fill);
    }

    if (hypoxia > 0.001f)
    {
        Color veil = BLACK;
        veil.a = (unsigned char)(255.0f * hypoxia);
        DrawRectangle(
            (int)bounds.x,
            (int)bounds.y,
            (int)bounds.width,
            (int)bounds.height,
            veil);
    }
}

void SceneOverlayMgr::draw(Rectangle bounds) const
{
    const float vignette = combinedVignetteOcclusion();
    Color fadeColor = BLACK;
    const float fadeOpacity = combinedFadeOpacity(fadeColor);

    drawVignette(bounds, vignette);
    if (sequenceHypoxiaOpacity > 0.001f || !hypoxiaDots.empty())
        drawHypoxiaOverlay(bounds);
    drawFadeOverlay(bounds, fadeOpacity, fadeColor);
}

}