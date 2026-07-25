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

#ifndef SCENE_CONTROLLER_H
#define SCENE_CONTROLLER_H

#include <ActiveScene.h>
#include <AudioManager.h>
#include <MaskEvaluator.h>
#include <MovementResolver.h>
#include <SceneLoader.h>
#include <functional>
#include <string>

namespace timberline_engine
{

class InventoryMgr;
class InteractionMgr;
class ItemDatabase;
class MilestoneManager;
class TakeMgr;
class WorldState;

class SceneController
{
    public:
    SceneController(
        SceneDatabase& sceneDatabase,
        AudioManager& audioManager);

    const ActiveScene& getActiveScene() const { return activeScene; }
    ActiveScene& getActiveScene() { return activeScene; }
    const std::string& getCurrentSceneId() const { return activeScene.getId(); }

    bool loadInitialScene(const std::string& sceneId, WorldState& worldState);
    bool transitionToScene(
        const std::string& nextSceneId,
        const std::string& nextSubSceneId,
        WorldState& worldState,
        TakeMgr& takeMgr,
        InteractionMgr& interactionMgr,
        InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase,
        const MilestoneManager& milestoneMgr,
        const std::function<bool(const std::string& phaseId)>& isPhaseComplete = nullptr);
    bool tryMove(
        const std::string& direction,
        WorldState& worldState,
        TakeMgr& takeMgr,
        InteractionMgr& interactionMgr,
        InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase,
        const MilestoneManager& milestoneMgr,
        const std::function<bool(const std::string& phaseId)>& isPhaseComplete = nullptr);
    bool isDirectionAvailable(
        const std::string& direction,
        const WorldState& worldState,
        const InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase,
        const MilestoneManager& milestoneMgr) const;

    MaskEvalContext buildMaskContext(
        const WorldState& worldState,
        const InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase,
        const MilestoneManager& milestoneMgr) const;

    private:
    bool applySceneStruct(
        const LocationStruct& locationStruct,
        const std::string& fromRoom,
        WorldState& worldState);

    SceneDatabase& sceneDatabase;
    AudioManager& audioManager;
    ActiveScene activeScene;
};

}

#endif /* SCENE_CONTROLLER_H */