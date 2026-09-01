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

#include "SceneController.h"

#include <InventoryMgr.h>
#include <InteractionMgr.h>
#include <ItemDatabase.h>
#include <SceneInventory.h>
#include <TakeMgr.h>
#include <WorldState.h>

namespace timberline_engine
{

SceneController::SceneController(
    SceneDatabase& sceneDatabase_,
    AudioManager& audioManager_)
    : sceneDatabase(sceneDatabase_),
      audioManager(audioManager_)
{
}

bool SceneController::loadInitialScene(const std::string& sceneId, WorldState& worldState)
{
    const SceneData* scene = sceneDatabase.getScene(sceneId);
    if (scene == nullptr)
        return false;

    worldState.activeSubSceneId = sceneDatabase.resolveActiveSubSceneId(
        *scene,
        worldState.storyFlags,
        "",
        SubSceneResolveMode::OnEnter);

    LocationStruct locationStruct;
    if (!sceneDatabase.loadScene(sceneId, worldState.activeSubSceneId, locationStruct))
        return false;

    activeScene.loadFromStruct(sceneId, locationStruct);
    return true;
}

MaskEvalContext SceneController::buildMaskContext(
    const WorldState& worldState,
    const InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    const MilestoneManager& milestoneMgr) const
{
    MaskEvalContext context;
    context.storyFlags = &worldState.storyFlags;
    context.milestoneMgr = &milestoneMgr;
    context.inventoryMgr = &inventoryMgr;
    context.itemDatabase = &itemDatabase;
    context.activeSubSceneId = worldState.activeSubSceneId;
    context.currentDay = worldState.day;
    context.saloonRoomPurchasedDay = worldState.saloonRoomPurchasedDay;
    context.sceneInventoryHasItem = [&](const std::string& defId)
    {
        return sceneInventoryHasItem(worldState.sceneInventories, worldState.currentSceneId, defId);
    };
    context.sceneInventoryHasItemFlag = [&](const std::string&)
    {
        return false;
    };
    return context;
}

bool SceneController::applySceneStruct(
    const LocationStruct& locationStruct,
    const std::string& fromRoom,
    WorldState& worldState)
{
    activeScene.loadFromStruct(worldState.currentSceneId, locationStruct);
    worldState.narrativeText = locationStruct.locationDescription;
    worldState.sceneVisits.resetForNewScene();
    worldState.committedPlayerDialogLines.clear();
    audioManager.onRoomEnter(
        sceneDatabase.getSceneAudio(worldState.currentSceneId, worldState.activeSubSceneId),
        fromRoom);
    return true;
}

bool SceneController::transitionToScene(
    const std::string& nextSceneId,
    const std::string& nextSubSceneId,
    WorldState& worldState,
    TakeMgr& takeMgr,
    InteractionMgr& interactionMgr,
    InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    const MilestoneManager& milestoneMgr,
    const std::function<bool(const std::string& phaseId)>& isPhaseComplete)
{
    if (nextSceneId.empty())
        return false;

    const SceneData* nextScene = sceneDatabase.getScene(nextSceneId);
    if (nextScene == nullptr)
        return false;

    const std::string resolvedSubSceneId = sceneDatabase.resolveActiveSubSceneId(
        *nextScene,
        worldState.storyFlags,
        nextSubSceneId,
        SubSceneResolveMode::OnEnter,
        isPhaseComplete);

    // Defer GPU texture upload to GameSession::refreshSceneImage (JobSystem).
    // Keep the previous room image drawn until that completes.
    LocationStruct nextLocation;
    if (!sceneDatabase.loadScene(nextSceneId, resolvedSubSceneId, nextLocation, false))
        return false;

    const std::string fromSceneId = worldState.currentSceneId;
    const std::string fromSubSceneId = worldState.activeSubSceneId;
    audioManager.onRoomExit(
        sceneDatabase.getSceneAudio(fromSceneId, fromSubSceneId),
        nextSceneId);

    worldState.previousSceneId = fromSceneId;
    worldState.previousSubSceneId = fromSubSceneId;
    worldState.currentSceneId = nextSceneId;
    worldState.activeSubSceneId = resolvedSubSceneId;

    const MaskEvalContext context = buildMaskContext(
        worldState,
        inventoryMgr,
        itemDatabase,
        milestoneMgr);

    ensureSceneInventoryInitialized(
        worldState.sceneInventories,
        nextSceneId,
        nextScene->sceneInventory,
        context);

    applySceneStruct(nextLocation, fromSceneId, worldState);

    if (!nextLocation.isUnderConstruction)
    {
        worldState.previousSceneId.clear();
        worldState.previousSubSceneId.clear();
    }

    interactionMgr.close();
    takeMgr.close();
    return true;
}

bool SceneController::isDirectionAvailable(
    const std::string& direction,
    const WorldState& worldState,
    const InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    const MilestoneManager& milestoneMgr) const
{
    const SceneData* scene = sceneDatabase.getScene(worldState.currentSceneId);
    if (scene == nullptr)
        return false;

    const MaskEvalContext context = buildMaskContext(
        worldState,
        inventoryMgr,
        itemDatabase,
        milestoneMgr);

    const MovementResolution resolution = MovementResolver::resolveDirection(
        sceneDatabase,
        *scene,
        worldState.activeSubSceneId,
        direction,
        context);

    return resolution.available;
}

MovementBlockReason SceneController::getDirectionBlockReason(
    const std::string& direction,
    const WorldState& worldState,
    const InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    const MilestoneManager& milestoneMgr) const
{
    const SceneData* scene = sceneDatabase.getScene(worldState.currentSceneId);
    if (scene == nullptr)
        return MovementBlockReason::None;

    const MaskEvalContext context = buildMaskContext(
        worldState,
        inventoryMgr,
        itemDatabase,
        milestoneMgr);

    return MovementResolver::blockReasonForDirection(
        sceneDatabase,
        *scene,
        worldState.activeSubSceneId,
        direction,
        context);
}

bool SceneController::tryMove(
    const std::string& direction,
    WorldState& worldState,
    TakeMgr& takeMgr,
    InteractionMgr& interactionMgr,
    InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    const MilestoneManager& milestoneMgr,
    const std::function<bool(const std::string& phaseId)>& isPhaseComplete)
{
    const LocationStruct& view = activeScene.getView();

    if (view.isUnderConstruction)
    {
        if (direction != "backward" || worldState.previousSceneId.empty())
            return false;

        return transitionToScene(
            worldState.previousSceneId,
            worldState.previousSubSceneId,
            worldState,
            takeMgr,
            interactionMgr,
            inventoryMgr,
            itemDatabase,
            milestoneMgr,
            isPhaseComplete);
    }

    const SceneData* scene = sceneDatabase.getScene(worldState.currentSceneId);
    if (scene == nullptr)
        return false;

    const MaskEvalContext context = buildMaskContext(
        worldState,
        inventoryMgr,
        itemDatabase,
        milestoneMgr);

    const MovementResolution resolution = MovementResolver::resolveDirection(
        sceneDatabase,
        *scene,
        worldState.activeSubSceneId,
        direction,
        context);

    if (!resolution.available || resolution.target.empty())
        return false;

    return transitionToScene(
        resolution.target.sceneId,
        resolution.target.subSceneId,
        worldState,
        takeMgr,
        interactionMgr,
        inventoryMgr,
        itemDatabase,
        milestoneMgr,
        isPhaseComplete);
}

}