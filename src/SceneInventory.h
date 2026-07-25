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

#ifndef SCENE_INVENTORY_H
#define SCENE_INVENTORY_H

#include <ItemInstance.h>
#include <MaskEvaluator.h>
#include <SceneInventoryDef.h>
#include <map>
#include <string>
#include <vector>

namespace timberline_engine
{

using SceneInventoryState = std::map<std::string, std::vector<ItemInstance>>;

void ensureSceneInventoryInitialized(
    SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::vector<SceneInventoryEntryDef>& definitions,
    const MaskEvalContext& context);

std::vector<const ItemInstance*> getVisibleSceneInventory(
    const SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::vector<SceneInventoryEntryDef>& definitions,
    const MaskEvalContext& context);

bool sceneInventoryHasItem(
    const SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::string& defId);

bool removeSceneInventoryItem(
    SceneInventoryState& inventories,
    const std::string& sceneId,
    const std::string& instanceId);

}

#endif /* SCENE_INVENTORY_H */