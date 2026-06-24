#include "WorldState.h"

#include <InventoryMgr.h>
namespace testgame
{

SavedGameState WorldState::snapshot(
    const ConversationManager& conversationMgr,
    const MilestoneManager& milestoneMgr,
    const InventoryMgr& inventoryMgr) const
{
    SavedGameState state;
    state.sceneId = currentSceneId;
    state.previousSceneId = previousSceneId;
    state.narrativeText = narrativeText;
    state.health = playerStats.health;
    state.energy = playerStats.energy;
    state.resolve = playerStats.resolve;
    state.lucidity = playerStats.lucidity;
    state.charisma = playerStats.charisma;
    state.walletCash = playerStats.walletCash;
    state.hasSpokenInCurrentScene = sceneVisits.hasSpokenInCurrentScene;
    state.hasUsedInCurrentScene = sceneVisits.hasUsedInCurrentScene;
    state.examinedSceneIds = sceneVisits.examinedSceneIds;
    state.usedSceneIds = sceneVisits.usedSceneIds;
    state.takenItemKeys = takenItemKeys;
    state.usedInteractionKeys = usedInteractionKeys;
    state.storyFlags = storyFlags;
    state.consumedStatusActions = playerStats.consumedStatusActions;
    state.committedPlayerDialogLines = committedPlayerDialogLines;
    state.inventoryItems = inventoryMgr.exportItemSnapshots();
    state.droppedItemsByScene = droppedItemsByScene;
    state.day = day;
    state.actionCount = actionCount;
    state.saloonRoomPurchasedDay = saloonRoomPurchasedDay;
    state.actorOpinions = actorOpinions;
    state.knownActorIds = knownActorIds;
    conversationMgr.exportPersistState(state.conversation);
    milestoneMgr.exportPersistState(state.milestones);
    return state;
}

bool WorldState::restore(
    const SavedGameState& state,
    ConversationManager& conversationMgr,
    MilestoneManager& milestoneMgr,
    InventoryMgr& inventoryMgr)
{
    currentSceneId = state.sceneId;
    previousSceneId = state.previousSceneId;
    narrativeText = state.narrativeText;
    playerStats.health = state.health;
    playerStats.energy = state.energy;
    playerStats.resolve = state.resolve;
    playerStats.lucidity = state.lucidity;
    playerStats.charisma = state.charisma;
    playerStats.walletCash = state.walletCash;
    sceneVisits.hasSpokenInCurrentScene = state.hasSpokenInCurrentScene;
    sceneVisits.hasUsedInCurrentScene = state.hasUsedInCurrentScene;
    sceneVisits.examinedSceneIds = state.examinedSceneIds;
    sceneVisits.usedSceneIds = state.usedSceneIds;
    takenItemKeys = state.takenItemKeys;
    usedInteractionKeys = state.usedInteractionKeys;
    storyFlags = state.storyFlags;
    playerStats.consumedStatusActions = state.consumedStatusActions;
    committedPlayerDialogLines = state.committedPlayerDialogLines;
    droppedItemsByScene = state.droppedItemsByScene;
    day = state.day;
    actionCount = state.actionCount;
    saloonRoomPurchasedDay = state.saloonRoomPurchasedDay;
    actorOpinions = state.actorOpinions;
    knownActorIds = state.knownActorIds;

    inventoryMgr.restoreFromSnapshots(state.inventoryItems);
    conversationMgr.importPersistState(state.conversation);
    milestoneMgr.importPersistState(state.milestones);
    return !currentSceneId.empty();
}

bool WorldState::isActorKnown(const std::string& actorId) const
{
    return !actorId.empty() && knownActorIds.count(actorId) > 0;
}

void WorldState::markActorKnown(const std::string& actorId)
{
    if (!actorId.empty())
        knownActorIds.insert(actorId);
}

int WorldState::actorOpinionOf(const std::string& actorId) const
{
    if (actorId.empty())
        return 0;

    std::map<std::string, int>::const_iterator it = actorOpinions.find(actorId);
    if (it == actorOpinions.end())
        return 0;

    return it->second;
}

void WorldState::applyActorOpinionDelta(const std::string& actorId, int delta)
{
    if (actorId.empty() || delta == 0)
        return;

    actorOpinions[actorId] += delta;
}

}