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

#ifndef MOVEMENT_RESOLVER_H
#define MOVEMENT_RESOLVER_H

#include <MaskEvaluator.h>
#include <MovementBlockReason.h>
#include <MovementMappingDef.h>
#include <SceneLoader.h>
#include <string>

namespace timberline_engine
{

struct MovementResolution
{
    bool available = false;
    MovementTarget target;
    std::string mappingId;
};

class MovementResolver
{
    public:
    static bool isDirectionBlanketed(
        const SceneData& scene,
        const std::string& activeSubSceneId,
        const std::string& direction);

    static bool isMappingEffectivelyMasked(
        const MovementMappingDef& mapping,
        const MaskEvalContext& context);

    static bool targetRequiresLight(
        const SceneDatabase& database,
        const MovementTarget& target);

    static bool hasIllumination(
        const SceneData& scene,
        const std::string& activeSubSceneId,
        const MaskEvalContext& context);

    static MovementResolution resolveDirection(
        const SceneDatabase& database,
        const SceneData& scene,
        const std::string& activeSubSceneId,
        const std::string& direction,
        const MaskEvalContext& context);

    /**
     * Classify why an advertised direction is blocked.
     * Returns None when the direction is available or there is no exit.
     * NeedsLight only when darkness / missing light source is the blocking reason
     * (not locked doors, story flags, climbing gear, etc.).
     */
    static MovementBlockReason blockReasonForDirection(
        const SceneDatabase& database,
        const SceneData& scene,
        const std::string& activeSubSceneId,
        const std::string& direction,
        const MaskEvalContext& context);
};

}

#endif /* MOVEMENT_RESOLVER_H */