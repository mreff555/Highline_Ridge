#include "SceneController.h"

#include <InventoryMgr.h>
#include <InteractionMgr.h>
#include <ItemDatabase.h>
#include <TakeMgr.h>
#include <WorldState.h>

namespace testgame
{

SceneController::SceneController(
    SceneDatabase& sceneDatabase_,
    AudioManager& audioManager_)
    : sceneDatabase(sceneDatabase_),
      audioManager(audioManager_)
{
}

bool SceneController::loadInitialScene(const std::string& sceneId)
{
    LocationStruct locationStruct;
    if (!sceneDatabase.loadScene(sceneId, locationStruct))
        return false;

    activeScene.loadFromStruct(sceneId, locationStruct);
    return true;
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
    audioManager.onRoomEnter(sceneDatabase.getSceneAudio(worldState.currentSceneId), fromRoom);
    return true;
}

bool SceneController::transitionToScene(
    const std::string& nextSceneId,
    WorldState& worldState,
    TakeMgr& takeMgr,
    InteractionMgr& interactionMgr)
{
    if (nextSceneId.empty())
        return false;

    LocationStruct nextLocation;
    if (!sceneDatabase.loadScene(nextSceneId, nextLocation))
        return false;

    activeScene.unloadOwnedImage();

    const std::string fromSceneId = worldState.currentSceneId;
    audioManager.onRoomExit(sceneDatabase.getSceneAudio(fromSceneId), nextSceneId);
    worldState.previousSceneId = fromSceneId;
    worldState.currentSceneId = nextSceneId;

    applySceneStruct(nextLocation, fromSceneId, worldState);

    if (!nextLocation.isUnderConstruction)
        worldState.previousSceneId.clear();

    interactionMgr.close();
    takeMgr.close();
    return true;
}

bool SceneController::hasLightSourceInInventory(
    const InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase) const
{
    for (const InventoryItem& item : inventoryMgr.exportItemSnapshots())
    {
        const ItemDef* def = itemDatabase.getDef(item.id);
        if (def != nullptr && def->lightSource)
            return true;
    }
    return false;
}

bool SceneController::canUseExit(
    const std::string& direction,
    const WorldState& worldState,
    const InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    std::string& outBlockedDetails) const
{
    ExitRequirementDef requirement;
    if (!sceneDatabase.getExitRequirement(activeScene.getId(), direction, requirement))
        return true;

    if (requirement.requiresLightSource && !hasLightSourceInInventory(inventoryMgr, itemDatabase))
    {
        outBlockedDetails = requirement.blockedDetails;
        return false;
    }

    if (requirement.requiresRoomPurchasedToday)
    {
        const bool roomValidToday = worldState.saloonRoomPurchasedDay > 0
            && worldState.saloonRoomPurchasedDay == worldState.day;
        if (!roomValidToday)
        {
            outBlockedDetails = requirement.blockedDetails;
            return false;
        }
    }

    return true;
}

bool SceneController::tryMove(
    const std::string& direction,
    WorldState& worldState,
    TakeMgr& takeMgr,
    InteractionMgr& interactionMgr,
    InventoryMgr& inventoryMgr,
    const ItemDatabase& itemDatabase,
    std::string& outBlockedDetails)
{
    const LocationStruct& view = activeScene.getView();

    if (view.isUnderConstruction)
    {
        if (direction != "backward" || worldState.previousSceneId.empty())
            return false;

        LocationStruct previousLocation;
        if (!sceneDatabase.loadScene(worldState.previousSceneId, previousLocation))
            return false;

        activeScene.unloadOwnedImage();

        const std::string exitSceneId = worldState.currentSceneId;
        const std::string returnSceneId = worldState.previousSceneId;
        audioManager.onRoomExit(sceneDatabase.getSceneAudio(worldState.currentSceneId), returnSceneId);
        worldState.previousSceneId.clear();
        worldState.currentSceneId = returnSceneId;
        applySceneStruct(previousLocation, exitSceneId, worldState);
        return true;
    }

    const std::string nextSceneId = sceneDatabase.getExitSceneId(worldState.currentSceneId, direction);
    if (nextSceneId.empty())
        return false;

    if (!canUseExit(direction, worldState, inventoryMgr, itemDatabase, outBlockedDetails))
        return false;

    return transitionToScene(nextSceneId, worldState, takeMgr, interactionMgr);
}

}