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

#include "SceneInventory.h"

namespace timberline_engine
{

namespace
{

bool inventoryContainsInstance(
    const std::vector<ItemInstance>& inventory,
    const std::string& instanceId)
{
    for (const ItemInstance& item : inventory)
    {
        if (item.instanceId == instanceId)
            return true;
    }
    return false;
}

bool definitionIsVisible(
    const SceneInventoryEntryDef& definition,
    const MaskEvalContext& context)
{
    return isMaskConditionMet(definition.unmaskWhen, context);
}

}

void ensureSceneInventoryInitialized(
    SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::vector<SceneInventoryEntryDef>& definitions,
    const MaskEvalContext& context)
{
    (void)context;

    std::vector<ItemInstance>& inventory = inventories[sceneId];
    for (const SceneInventoryEntryDef& definition : definitions)
    {
        if (inventoryContainsInstance(inventory, definition.instance.instanceId))
            continue;

        inventory.push_back(definition.instance);
    }
}

std::vector<const ItemInstance*> getVisibleSceneInventory(
    const SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::vector<SceneInventoryEntryDef>& definitions,
    const MaskEvalContext& context)
{
    std::vector<const ItemInstance*> visible;
    SceneInventoryState::const_iterator inventoryIt = inventories.find(sceneId);
    if (inventoryIt == inventories.end())
        return visible;

    for (const ItemInstance& item : inventoryIt->second)
    {
        for (const SceneInventoryEntryDef& definition : definitions)
        {
            if (definition.instance.instanceId != item.instanceId)
                continue;

            if (!definitionIsVisible(definition, context))
                break;

            visible.push_back(&item);
            break;
        }
    }

    return visible;
}

bool sceneInventoryHasItem(
    const SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::string& defId)
{
    SceneInventoryState::const_iterator inventoryIt = inventories.find(sceneId);
    if (inventoryIt == inventories.end())
        return false;

    for (const ItemInstance& item : inventoryIt->second)
    {
        if (item.defId == defId)
            return true;
    }

    return false;
}

bool removeSceneInventoryItem(
    SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::string& instanceId)
{
    SceneInventoryState::iterator inventoryIt = inventories.find(sceneId);
    if (inventoryIt == inventories.end())
        return false;

    std::vector<ItemInstance>& inventory = inventoryIt->second;
    for (std::vector<ItemInstance>::iterator it = inventory.begin(); it != inventory.end(); ++it)
    {
        if (it->instanceId != instanceId)
            continue;

        inventory.erase(it);
        return true;
    }

    return false;
}

}