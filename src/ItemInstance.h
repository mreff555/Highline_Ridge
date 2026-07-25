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

#ifndef ITEM_INSTANCE_H
#define ITEM_INSTANCE_H

#include <ItemDef.h>
#include <string>
#include <vector>

namespace timberline_engine
{
class ItemDatabase;
}

namespace timberline_engine
{

struct ItemInstance
{
    std::string instanceId;
    std::string defId;
    int quantity = 1;
    float usedFraction = 0.0f;
    std::vector<std::string> activeFlags;
    std::vector<ItemInstance> contents;

    bool hasFlag(const std::string& flag) const;
};

float computeItemWeightLb(const ItemDef& def, const ItemInstance& instance);
float computeContainerTotalWeightLb(
    const ItemDef& def,
    const ItemInstance& instance,
    const ItemDef* (*resolveDef)(const std::string& defId));

float computeContainerTotalWeightLb(
    const ItemDef& def,
    const ItemInstance& instance,
    const ItemDatabase& database);

}

#endif /* ITEM_INSTANCE_H */