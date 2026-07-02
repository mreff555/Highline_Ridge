#ifndef WORLD_STATE_H
#define WORLD_STATE_H

#include <ConversationManager.h>
#include <ItemInstance.h>
#include <MilestoneManager.h>
#include <PlayerStats.h>
#include <SaveGame.h>
#include <SceneVisitTracker.h>
#include <SceneInventory.h>
#include <TakeableItemDef.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace highline_ridge
{

class ConversationManager;
class InventoryMgr;

class WorldState
{
    public:
    std::string currentSceneId;
    std::string activeSubSceneId;
    std::string previousSceneId;
    std::string previousSubSceneId;
    std::string narrativeText;
    PlayerStats playerStats;
    SceneVisitTracker sceneVisits;
    std::set<std::string> takenItemKeys;
    std::set<std::string> usedInteractionKeys;
    std::set<std::string> storyFlags;
    std::set<std::string> committedPlayerDialogLines;
    std::set<std::string> knownActorIds;
    std::map<std::string, std::vector<TakeableItemDef>> droppedItemsByScene;
    SceneInventoryState sceneInventories;
    int day = 1;
    int actionCount = 0;
    int saloonRoomPurchasedDay = 0;
    int lucidityCollapseCount = 0;
    int lastLucidityCollapseDay = 0;
    int lastSleepDay = 0;
    std::map<std::string, int> flagGrantedDay;
    std::map<std::string, int> actorOpinions;
    std::map<std::string, float> actorTabOwed;

    void recordAction() { ++actionCount; }
    bool isActorKnown(const std::string& actorId) const;
    void markActorKnown(const std::string& actorId);
    int actorOpinionOf(const std::string& actorId) const;
    void applyActorOpinionDelta(const std::string& actorId, int delta);
    float actorTabOwedTo(const std::string& actorId) const;
    void applyActorTabDelta(const std::string& actorId, float delta);
    void advanceDay() { ++day; }

    SavedGameState snapshot(
        const ConversationManager& conversationMgr,
        const MilestoneManager& milestoneMgr,
        const InventoryMgr& inventoryMgr) const;

    bool restore(
        const SavedGameState& state,
        ConversationManager& conversationMgr,
        MilestoneManager& milestoneMgr,
        InventoryMgr& inventoryMgr);
};

}

#endif /* WORLD_STATE_H */