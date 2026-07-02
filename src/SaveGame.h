#ifndef SAVE_GAME_H
#define SAVE_GAME_H

#include <InventoryItem.h>
#include <ItemInstance.h>
#include <MilestoneStruct.h>
#include <TakeableItemDef.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace highline_ridge
{

struct ConversationPersistState
{
    std::set<std::string> completedPhaseIds;
    std::set<std::string> completedRandomLineIds;
    std::set<std::string> consumedScriptedChoiceIds;
    std::set<std::string> persistedConsumedScriptedChoiceIds;
};

struct SavedGameState
{
    std::string sceneId;
    std::string activeSubSceneId;
    std::string previousSceneId;
    std::string previousSubSceneId;
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
    std::set<std::string> usedInteractionKeys;
    std::set<std::string> storyFlags;
    std::set<std::string> consumedStatusActions;
    std::set<std::string> committedPlayerDialogLines;
    std::vector<InventoryItem> inventoryItems;
    std::map<std::string, std::vector<TakeableItemDef>> droppedItemsByScene;
    std::map<std::string, std::vector<ItemInstance>> sceneInventories;
    ConversationPersistState conversation;
    MilestonePersistState milestones;
    int day = 1;
    int actionCount = 0;
    int saloonRoomPurchasedDay = 0;
    int lucidityCollapseCount = 0;
    int lastLucidityCollapseDay = 0;
    int lastSleepDay = 0;
    std::map<std::string, int> flagGrantedDay;
    std::map<std::string, int> actorOpinions;
    std::map<std::string, float> actorTabOwed;
    std::set<std::string> knownActorIds;
};

struct SaveSlotMetadata
{
    std::string name;
    std::string timestampIso;
    long long unixTime = 0;
    bool isQuickSave = false;
};

struct SaveSlotListing
{
    SaveSlotMetadata metadata;
    std::string filePath;
};

const char* defaultSavePath();
const char* savesDirectory();
const char* quickSavePath();

bool saveFileExists(const std::string& path);
std::string currentTimestampIso(long long& outUnixTime);
std::string formatSaveTimestampForDisplay(const SaveSlotMetadata& metadata);
std::vector<SaveSlotListing> listSaveSlots();
bool readSaveMetadata(const std::string& path, SaveSlotMetadata& outMetadata);
bool writeSaveFile(const std::string& path, const SavedGameState& state, const SaveSlotMetadata& metadata);
bool readSaveFile(const std::string& path, SavedGameState& state, SaveSlotMetadata* outMetadata = nullptr);
bool deleteSaveFile(const std::string& path);
std::string buildNamedSavePath(long long unixTime);
void enforceNamedSaveLimit(int maxNamedSaves);

}

#endif /* SAVE_GAME_H */