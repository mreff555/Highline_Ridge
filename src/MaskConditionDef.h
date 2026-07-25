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

#ifndef MASK_CONDITION_DEF_H
#define MASK_CONDITION_DEF_H

#include <MilestoneStruct.h>
#include <string>
#include <vector>

namespace timberline_engine
{

enum class MaskConditionType
{
    None,
    All,
    Any,
    Flag,
    MilestoneStarted,
    MilestoneComplete,
    MilestoneFailed,
    PlayerHasItem,
    PlayerHasItemFlag,
    SubSceneActive
};

struct MaskCondition
{
    MaskConditionType type = MaskConditionType::None;
    std::string value;
    std::vector<MaskCondition> children;
};

struct MilestoneMaskHook
{
    std::string milestoneId;
    MilestoneStatus when = MilestoneStatus::Complete;
};

}

#endif /* MASK_CONDITION_DEF_H */