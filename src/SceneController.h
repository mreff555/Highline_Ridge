#ifndef SCENE_CONTROLLER_H
#define SCENE_CONTROLLER_H

#include <ActiveScene.h>
#include <AudioManager.h>
#include <SceneLoader.h>
#include <string>

namespace testgame
{

class InventoryMgr;
class InteractionMgr;
class ItemDatabase;
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

    bool loadInitialScene(const std::string& sceneId);
    bool transitionToScene(
        const std::string& nextSceneId,
        WorldState& worldState,
        TakeMgr& takeMgr,
        InteractionMgr& interactionMgr);
    bool tryMove(
        const std::string& direction,
        WorldState& worldState,
        TakeMgr& takeMgr,
        InteractionMgr& interactionMgr,
        InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase,
        std::string& outBlockedDetails);
    bool canUseExit(
        const std::string& direction,
        const WorldState& worldState,
        const InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase,
        std::string& outBlockedDetails) const;

    private:
    bool hasLightSourceInInventory(
        const InventoryMgr& inventoryMgr,
        const ItemDatabase& itemDatabase) const;

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