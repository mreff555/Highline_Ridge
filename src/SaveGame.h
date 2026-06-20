#ifndef SAVE_GAME_H
#define SAVE_GAME_H

#include <InventoryItem.h>
#include <MilestoneStruct.h>
#include <set>
#include <string>
#include <vector>

namespace testgame
{

struct ConversationPersistState
{
    std::set<std::string> completedPhaseIds;
    std::set<std::string> completedRandomLineIds;
    std::set<std::string> consumedScriptedChoiceIds;
};

struct SavedGameState
{
    std::string sceneId;
    std::string previousSceneId;
    std::string narrativeText;
    float health = 90.0f;
    float energy = 20.0f;
    float resolve = 50.0f;
    float lucidity = 30.0f;
    float charisma = 50.0f;
    float walletCash = 20.0f;
    bool hasSpokenInCurrentScene = false;
    bool hasUsedInCurrentScene = false;
    std::set<std::string> examinedSceneIds;
    std::set<std::string> usedSceneIds;
    std::set<std::string> takenItemKeys;
    std::set<std::string> storyFlags;
    std::set<std::string> consumedStatusActions;
    std::set<std::string> committedPlayerDialogLines;
    std::vector<InventoryItem> inventoryItems;
    ConversationPersistState conversation;
    MilestonePersistState milestones;
};

const char* defaultSavePath();

bool saveFileExists(const std::string& path);
bool writeSaveFile(const std::string& path, const SavedGameState& state);
bool readSaveFile(const std::string& path, SavedGameState& state);

}

#endif /* SAVE_GAME_H */