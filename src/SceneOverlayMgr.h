/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#ifndef SCENE_OVERLAY_MGR_H
#define SCENE_OVERLAY_MGR_H

#include <SceneOverlayDef.h>
#include <raylib.h>
#include <functional>
#include <vector>

namespace timberline_engine
{

class SceneOverlayMgr
{
    public:
    void clear();
    void setSceneOverlays(const std::vector<SceneOverlayDef>& overlays);
    void startSequence(
        const std::vector<OverlaySequenceStep>& steps,
        std::function<void()> onComplete = nullptr);
    void setOnSequenceStepStart(
        std::function<void(const OverlaySequenceStep&, float fromOpacity, float toOpacity)> callback);
    bool isSequenceActive() const { return sequenceActive; }
    float getHypoxiaOpacity() const { return sequenceHypoxiaOpacity; }
    void setHypoxiaOpacity(float opacity);
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

    struct HypoxiaDot
    {
        float nx = 0.0f;
        float ny = 0.0f;
        float radius = 4.0f;
        bool crimson = false;
        float stagger = 0.0f;
    };

    void advanceSequenceStep();
    void beginSequenceStep();
    void generateHypoxiaDots(const OverlaySequenceStep& step);
    void drawVignette(Rectangle bounds, float occlusionPercent) const;
    void drawFadeOverlay(Rectangle bounds, float opacity, Color color) const;
    void drawHypoxiaOverlay(Rectangle bounds) const;
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
    float stepStartHypoxiaOpacity = 0.0f;
    float sequenceHypoxiaOpacity = 0.0f;
    Color sequenceFadeColor = BLACK;
    std::vector<HypoxiaDot> hypoxiaDots;
    bool sequenceActive = false;
    bool sequenceCompletedPending = false;
    std::function<void()> sequenceCompleteCallback;
    std::function<void(const OverlaySequenceStep&, float fromOpacity, float toOpacity)> onSequenceStepStart;
};

}

#endif /* SCENE_OVERLAY_MGR_H */