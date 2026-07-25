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

#ifndef MOVEMENT_MAPPING_DEF_H
#define MOVEMENT_MAPPING_DEF_H

#include <MaskConditionDef.h>
#include <string>
#include <vector>

namespace timberline_engine
{

struct MovementTarget
{
    std::string sceneId;
    std::string subSceneId;

    bool empty() const { return sceneId.empty(); }
};

struct MovementMappingDef
{
    std::string id;
    MovementTarget target;
    bool defaultMasked = false;
    std::string exclusiveGroup;
    MaskCondition unmaskWhen;
    std::vector<MilestoneMaskHook> onMilestoneEnable;
    std::vector<MilestoneMaskHook> onMilestoneDisable;
};

MovementTarget parseMovementTarget(const std::string& rawTarget);

}

#endif /* MOVEMENT_MAPPING_DEF_H */