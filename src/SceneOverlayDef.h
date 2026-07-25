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

#ifndef SCENE_OVERLAY_DEF_H
#define SCENE_OVERLAY_DEF_H

#include <nlohmann/json_fwd.hpp>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
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
    VignetteTo,
    HypoxiaTo
};

struct OverlaySequenceStep
{
    OverlaySequenceAction action = OverlaySequenceAction::Hold;
    float targetOpacity = 0.0f;
    float targetOcclusionPercent = 0.0f;
    float durationSeconds = 0.0f;
    Color color = BLACK;
    std::string sfxPath;
    float sfxVolume = 1.0f;
};

bool parseOverlayColor(const nlohmann::json& value, Color& outColor);
bool parseOverlayAnimate(const nlohmann::json& animate, OverlayAnimateDef& out);
bool parseSceneOverlay(const nlohmann::json& overlay, SceneOverlayDef& out);
bool parseSceneOverlays(const nlohmann::json& overlays, std::vector<SceneOverlayDef>& out);
bool parseOverlaySequenceStep(const nlohmann::json& step, OverlaySequenceStep& out);
bool parseOverlaySequence(const nlohmann::json& sequence, std::vector<OverlaySequenceStep>& out);

}

#endif /* SCENE_OVERLAY_DEF_H */