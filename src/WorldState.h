#ifndef WORLD_STATE_H
#define WORLD_STATE_H

#include <ConversationManager.h>
#include <ItemInstance.h>
#include <MilestoneManager.h>
#include <PlayerStats.h>
#include <SaveGame.h>
#include <SceneVisitTracker.h>
#include <TakeableItemDef.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace testgame
{

class ConversationManager;
class InventoryMgr;

class WorldState
{
    public:
    std::string currentSceneId;
    std::string previousSceneId;
    std::string narrativeText;
    PlayerStats playerStats;
    SceneVisitTracker sceneVisits;
    std::set<std::string> takenItemKeys;
    std::set<std::string> usedInteractionKeys;
    std::set<std::string> storyFlags;
    std::set<std::string> committedPlayerDialogLines;
    std::map<std::string, std::vector<TakeableItemDef>> droppedItemsByScene;
    int day = 1;
    int actionCount = 0;
    int saloonRoomPurchasedDay = 0;

    void recordAction() { ++actionCount; }
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