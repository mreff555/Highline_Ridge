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

#include "ItemInstance.h"

#include <ItemDatabase.h>
#include <algorithm>
#include <cmath>

namespace timberline_engine
{

namespace
{
    float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }
}

float roundItemWeightLb(float weightLb)
{
    return std::round(weightLb * 100.0f) / 100.0f;
}

bool hasItemFlag(const std::vector<std::string>& activeFlags, const std::string& flag)
{
    if (flag.empty())
        return false;

    return std::find(activeFlags.begin(), activeFlags.end(), flag) != activeFlags.end();
}

std::string resolveItemPath(
    const std::string& primaryPath,
    const std::string& alternatePath,
    const std::string& alternateFlag,
    const std::vector<std::string>& activeFlags)
{
    if (!alternatePath.empty() && hasItemFlag(activeFlags, alternateFlag))
        return alternatePath;
    return primaryPath;
}

bool ItemInstance::hasFlag(const std::string& flag) const
{
    return hasItemFlag(activeFlags, flag);
}

float computeItemWeightLb(const ItemDef& def, const ItemInstance& instance)
{
    if (def.quantity.stackable)
    {
        const float unitWeight = def.quantity.unitWeightLb > 0.0f
            ? def.quantity.unitWeightLb
            : def.weightLb;
        const float remaining = std::max(0.0f, 1.0f - clamp01(instance.usedFraction));
        return roundItemWeightLb(unitWeight * static_cast<float>(instance.quantity) * remaining);
    }

    return roundItemWeightLb(def.weightLb);
}

float computeContainerTotalWeightLb(
    const ItemDef& def,
    const ItemInstance& instance,
    const ItemDef* (*resolveDef)(const std::string& defId))
{
    float total = computeItemWeightLb(def, instance);
    if (!def.container.isContainer || resolveDef == nullptr)
        return total;

    for (const ItemInstance& child : instance.contents)
    {
        const ItemDef* childDef = resolveDef(child.defId);
        if (childDef == nullptr)
            continue;

        total += computeContainerTotalWeightLb(*childDef, child, resolveDef);
    }

    return roundItemWeightLb(total);
}

float computeContainerTotalWeightLb(
    const ItemDef& def,
    const ItemInstance& instance,
    const ItemDatabase& database)
{
    float total = computeItemWeightLb(def, instance);
    if (!def.container.isContainer)
        return total;

    for (const ItemInstance& child : instance.contents)
    {
        const ItemDef* childDef = database.getDef(child.defId);
        if (childDef == nullptr)
            continue;

        total += computeContainerTotalWeightLb(*childDef, child, database);
    }

    return roundItemWeightLb(total);
}

}