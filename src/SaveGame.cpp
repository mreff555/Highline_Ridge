#include "SaveGame.h"

#include <ItemInstance.h>
#include <PlatformPath.h>
#include <TakeableItemDef.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>

namespace highline_ridge
{

namespace
{

template<typename Set>
nlohmann::json setToJsonArray(const Set& values)
{
    nlohmann::json array = nlohmann::json::array();
    for (const typename Set::value_type& value : values)
        array.push_back(value);
    return array;
}

nlohmann::json actorOpinionsToJson(const std::map<std::string, int>& actorOpinions)
{
    nlohmann::json object = nlohmann::json::object();
    for (std::map<std::string, int>::const_iterator it = actorOpinions.begin();
         it != actorOpinions.end();
         ++it)
        object[it->first] = it->second;
    return object;
}

void actorOpinionsFromJson(const nlohmann::json& object, std::map<std::string, int>& outActorOpinions)
{
    outActorOpinions.clear();
    if (!object.is_object())
        return;

    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!it.value().is_number_integer())
            continue;
        outActorOpinions[it.key()] = it.value().get<int>();
    }
}

nlohmann::json actorTabOwedToJson(const std::map<std::string, float>& actorTabOwed)
{
    nlohmann::json object = nlohmann::json::object();
    for (std::map<std::string, float>::const_iterator it = actorTabOwed.begin();
         it != actorTabOwed.end();
         ++it)
        object[it->first] = it->second;
    return object;
}

void actorTabOwedFromJson(const nlohmann::json& object, std::map<std::string, float>& outActorTabOwed)
{
    outActorTabOwed.clear();
    if (!object.is_object())
        return;

    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!it.value().is_number())
            continue;
        const float tab = std::max(0.0f, it.value().get<float>());
        if (tab > 0.0f)
            outActorTabOwed[it.key()] = tab;
    }
}

template<typename Set>
void jsonArrayToSet(const nlohmann::json& array, Set& outValues)
{
    outValues.clear();
    if (!array.is_array())
        return;

    for (const nlohmann::json& entry : array)
    {
        if (entry.is_string())
            outValues.insert(entry.get<std::string>());
    }
}

bool ensureParentDirectory(const std::string& path)
{
    return ensureParentDirectories(path);
}

nlohmann::json itemInstanceToJson(const ItemInstance& instance)
{
    nlohmann::json contents = nlohmann::json::array();
    for (const ItemInstance& child : instance.contents)
        contents.push_back(itemInstanceToJson(child));

    return {
        {"instanceId", instance.instanceId},
        {"defId", instance.defId},
        {"quantity", instance.quantity},
        {"usedFraction", instance.usedFraction},
        {"activeFlags", instance.activeFlags},
        {"contents", contents}
    };
}

void itemInstanceFromJson(const nlohmann::json& entry, ItemInstance& outInstance)
{
    outInstance.instanceId = entry.value("instanceId", "");
    outInstance.defId = entry.value("defId", "");
    outInstance.quantity = entry.value("quantity", 1);
    outInstance.usedFraction = entry.value("usedFraction", 0.0f);
    outInstance.activeFlags.clear();

    const nlohmann::json& flags = entry.value("activeFlags", nlohmann::json::array());
    if (flags.is_array())
    {
        for (const nlohmann::json& flag : flags)
        {
            if (flag.is_string())
                outInstance.activeFlags.push_back(flag.get<std::string>());
        }
    }

    outInstance.contents.clear();
    const nlohmann::json& contents = entry.value("contents", nlohmann::json::array());
    if (!contents.is_array())
        return;

    for (const nlohmann::json& child : contents)
    {
        if (!child.is_object())
            continue;

        ItemInstance parsed;
        itemInstanceFromJson(child, parsed);
        if (!parsed.defId.empty())
            outInstance.contents.push_back(parsed);
    }
}

nlohmann::json inventoryToJson(const std::vector<InventoryItem>& items)
{
    nlohmann::json array = nlohmann::json::array();
    for (const InventoryItem& item : items)
    {
        if (item.id.empty())
            continue;

        nlohmann::json entry = {
            {"id", item.id},
            {"name", item.name},
            {"iconPath", item.iconPath},
            {"examineImagePath", item.examineImagePath},
            {"examineText", item.examineText},
            {"weightLb", item.weightLb},
            {"instance", itemInstanceToJson(item.instance)}
        };
        if (item.isUndefined)
        {
            entry["isUndefined"] = true;
            entry["undefinedPurchaseSceneId"] = item.undefinedPurchaseSceneId;
        }
        array.push_back(entry);
    }
    return array;
}

std::string milestoneStatusToString(MilestoneStatus status)
{
    switch (status)
    {
    case MilestoneStatus::Started: return "started";
    case MilestoneStatus::Complete: return "complete";
    case MilestoneStatus::Failed: return "failed";
    case MilestoneStatus::NotStarted:
    default:
        return "not_started";
    }
}

MilestoneStatus milestoneStatusFromString(const std::string& value)
{
    if (value == "started")
        return MilestoneStatus::Started;
    if (value == "complete")
        return MilestoneStatus::Complete;
    if (value == "failed")
        return MilestoneStatus::Failed;
    return MilestoneStatus::NotStarted;
}

nlohmann::json milestonesToJson(const MilestonePersistState& milestones)
{
    nlohmann::json root = nlohmann::json::object();
    for (const std::pair<const std::string, MilestoneRuntimeEntry>& pair : milestones.entries)
    {
        if (pair.second.status == MilestoneStatus::NotStarted &&
            pair.second.completedObjectiveIds.empty() &&
            !pair.second.startedAt.isValid())
        {
            continue;
        }

        nlohmann::json entry;
        entry["status"] = milestoneStatusToString(pair.second.status);
        entry["completedObjectives"] = setToJsonArray(pair.second.completedObjectiveIds);
        if (pair.second.startedAt.isValid())
        {
            entry["startedAt"] = {
                {"gameDay", pair.second.startedAt.gameDay},
                {"gameHour", pair.second.startedAt.gameHour}
            };
        }
        root[pair.first] = entry;
    }
    return root;
}

void milestonesFromJson(const nlohmann::json& root, MilestonePersistState& outMilestones)
{
    outMilestones.entries.clear();
    if (!root.is_object())
        return;

    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (!it.value().is_object())
            continue;

        MilestoneRuntimeEntry entry;
        entry.status = milestoneStatusFromString(it.value().value("status", "not_started"));
        jsonArrayToSet(
            it.value().value("completedObjectives", nlohmann::json::array()),
            entry.completedObjectiveIds);

        const nlohmann::json& startedAt = it.value().value("startedAt", nlohmann::json::object());
        if (startedAt.is_object())
        {
            entry.startedAt.gameDay = startedAt.value("gameDay", -1);
            entry.startedAt.gameHour = startedAt.value("gameHour", -1);
        }

        outMilestones.entries[it.key()] = entry;
    }
}

nlohmann::json takeableToJson(const TakeableItemDef& item)
{
    return {
        {"id", item.id},
        {"name", item.name},
        {"iconPath", item.iconPath},
        {"examineImagePath", item.examineImagePath},
        {"examineText", item.examineText},
        {"requiresExamine", item.requiresExamine}
    };
}

void takeableFromJson(const nlohmann::json& entry, TakeableItemDef& outItem)
{
    outItem.id = entry.value("id", "");
    outItem.name = entry.value("name", "");
    outItem.iconPath = entry.value("iconPath", "");
    outItem.examineImagePath = entry.value("examineImagePath", "");
    outItem.examineText = entry.value("examineText", "");
    outItem.requiresExamine = entry.value("requiresExamine", true);
}

nlohmann::json sceneInventoriesToJson(const std::map<std::string, std::vector<ItemInstance>>& sceneInventories)
{
    nlohmann::json root = nlohmann::json::object();
    for (const std::pair<const std::string, std::vector<ItemInstance>>& pair : sceneInventories)
    {
        nlohmann::json items = nlohmann::json::array();
        for (const ItemInstance& item : pair.second)
            items.push_back(itemInstanceToJson(item));
        root[pair.first] = items;
    }
    return root;
}

void sceneInventoriesFromJson(
    const nlohmann::json& root,
    std::map<std::string, std::vector<ItemInstance>>& outSceneInventories)
{
    outSceneInventories.clear();
    if (!root.is_object())
        return;

    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (!it.value().is_array())
            continue;

        std::vector<ItemInstance> items;
        for (const nlohmann::json& entry : it.value())
        {
            ItemInstance instance;
            itemInstanceFromJson(entry, instance);
            if (!instance.defId.empty())
                items.push_back(instance);
        }

        if (!items.empty())
            outSceneInventories[it.key()] = items;
    }
}

nlohmann::json droppedItemsToJson(const std::map<std::string, std::vector<TakeableItemDef>>& droppedItems)
{
    nlohmann::json root = nlohmann::json::object();
    for (const std::pair<const std::string, std::vector<TakeableItemDef>>& pair : droppedItems)
    {
        nlohmann::json items = nlohmann::json::array();
        for (const TakeableItemDef& item : pair.second)
            items.push_back(takeableToJson(item));
        root[pair.first] = items;
    }
    return root;
}

void droppedItemsFromJson(
    const nlohmann::json& root,
    std::map<std::string, std::vector<TakeableItemDef>>& outDroppedItems)
{
    outDroppedItems.clear();
    if (!root.is_object())
        return;

    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (!it.value().is_array())
            continue;

        std::vector<TakeableItemDef> items;
        for (const nlohmann::json& entry : it.value())
        {
            if (!entry.is_object())
                continue;

            TakeableItemDef item;
            takeableFromJson(entry, item);
            if (!item.id.empty())
                items.push_back(item);
        }

        if (!items.empty())
            outDroppedItems[it.key()] = items;
    }
}

void inventoryFromJson(const nlohmann::json& array, std::vector<InventoryItem>& outItems)
{
    outItems.clear();
    if (!array.is_array())
        return;

    for (const nlohmann::json& entry : array)
    {
        if (!entry.is_object())
            continue;

        InventoryItem item;
        item.id = entry.value("id", "");
        item.name = entry.value("name", "");
        item.iconPath = entry.value("iconPath", "");
        item.examineImagePath = entry.value("examineImagePath", "");
        item.examineText = entry.value("examineText", "");
        item.weightLb = entry.value("weightLb", 0.0f);
        item.isUndefined = entry.value("isUndefined", false);
        item.undefinedPurchaseSceneId = entry.value("undefinedPurchaseSceneId", "");

        const nlohmann::json& instance = entry.value("instance", nlohmann::json::object());
        if (instance.is_object() && !instance.empty())
            itemInstanceFromJson(instance, item.instance);
        else
        {
            item.instance.instanceId = item.id;
            item.instance.defId = item.id;
        }

        if (!item.id.empty())
            outItems.push_back(item);
    }
}

nlohmann::json saveMetadataToJson(const SaveSlotMetadata& metadata)
{
    return {
        {"name", metadata.name},
        {"timestamp", metadata.timestampIso},
        {"unixTime", metadata.unixTime},
        {"isQuickSave", metadata.isQuickSave}
    };
}

bool saveMetadataFromJson(const nlohmann::json& json, SaveSlotMetadata& outMetadata)
{
    if (!json.is_object())
        return false;

    outMetadata.name = json.value("name", "");
    outMetadata.timestampIso = json.value("timestamp", "");
    outMetadata.unixTime = json.value("unixTime", 0LL);
    outMetadata.isQuickSave = json.value("isQuickSave", false);
    return !outMetadata.name.empty() && outMetadata.unixTime > 0;
}

bool isSaveFileName(const std::string& name)
{
    if (name.size() < 4)
        return false;
    return name.compare(name.size() - 4, 4, ".sav") == 0;
}

}

const char* defaultSavePath()
{
    return "saves/highline_ridge.sav";
}

const char* savesDirectory()
{
    return "saves";
}

const char* quickSavePath()
{
    return "saves/quick.sav";
}

std::string currentTimestampIso(long long& outUnixTime)
{
    const std::time_t now = std::time(nullptr);
    outUnixTime = static_cast<long long>(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &localTime);
    return buffer;
}

std::string formatSaveTimestampForDisplay(const SaveSlotMetadata& metadata)
{
    if (metadata.timestampIso.empty())
        return "";

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if (std::sscanf(
            metadata.timestampIso.c_str(),
            "%d-%d-%dT%d:%d",
            &year,
            &month,
            &day,
            &hour,
            &minute) < 5)
    {
        return metadata.timestampIso;
    }

    static const char* kMonthNames[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    const char* monthName = "???";
    if (month >= 1 && month <= 12)
        monthName = kMonthNames[month - 1];

    char buffer[48];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s %d, %d  %d:%02d",
        monthName,
        day,
        year,
        hour,
        minute);
    return buffer;
}

bool saveFileExists(const std::string& path)
{
    return std::ifstream(path.c_str()).good();
}

bool readSaveMetadata(const std::string& path, SaveSlotMetadata& outMetadata)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    const nlohmann::json& saveMeta = root.value("saveMeta", nlohmann::json::object());
    if (saveMetadataFromJson(saveMeta, outMetadata))
        return true;

    outMetadata.name = "Saved Game";
    outMetadata.timestampIso.clear();
    outMetadata.unixTime = 0;
    outMetadata.isQuickSave = false;
    return true;
}

std::vector<SaveSlotListing> listSaveSlots()
{
    std::vector<SaveSlotListing> listings;
    ensureDirectory(savesDirectory());

    for (const std::string& fileName : listDirectoryFileNames(savesDirectory()))
    {
        if (!isSaveFileName(fileName))
            continue;

        SaveSlotListing listing;
        listing.filePath = pathJoin(savesDirectory(), fileName);
        if (!readSaveMetadata(listing.filePath, listing.metadata))
            continue;

        if (listing.metadata.unixTime <= 0)
            continue;

        listings.push_back(listing);
    }

    std::vector<SaveSlotListing> quickSaves;
    std::vector<SaveSlotListing> namedSaves;
    quickSaves.reserve(listings.size());
    namedSaves.reserve(listings.size());

    for (const SaveSlotListing& listing : listings)
    {
        if (listing.metadata.isQuickSave)
            quickSaves.push_back(listing);
        else
            namedSaves.push_back(listing);
    }

    std::sort(
        namedSaves.begin(),
        namedSaves.end(),
        [](const SaveSlotListing& a, const SaveSlotListing& b)
        {
            return a.metadata.unixTime > b.metadata.unixTime;
        });

    std::vector<SaveSlotListing> ordered;
    ordered.reserve(quickSaves.size() + namedSaves.size());
    for (const SaveSlotListing& listing : quickSaves)
        ordered.push_back(listing);
    for (const SaveSlotListing& listing : namedSaves)
        ordered.push_back(listing);

    return ordered;
}

std::string buildNamedSavePath(long long unixTime)
{
    char fileName[64];
    std::snprintf(fileName, sizeof(fileName), "named_%lld.sav", unixTime);
    return pathJoin(savesDirectory(), fileName);
}

void enforceNamedSaveLimit(int maxNamedSaves)
{
    if (maxNamedSaves < 0)
        return;

    std::vector<SaveSlotListing> listings = listSaveSlots();
    std::vector<SaveSlotListing> namedSaves;
    for (const SaveSlotListing& listing : listings)
    {
        if (!listing.metadata.isQuickSave)
            namedSaves.push_back(listing);
    }

    std::sort(
        namedSaves.begin(),
        namedSaves.end(),
        [](const SaveSlotListing& a, const SaveSlotListing& b)
        {
            return a.metadata.unixTime < b.metadata.unixTime;
        });

    while ((int)namedSaves.size() >= maxNamedSaves)
    {
        deleteSaveFile(namedSaves.front().filePath);
        namedSaves.erase(namedSaves.begin());
    }
}

bool deleteSaveFile(const std::string& path)
{
    return std::remove(path.c_str()) == 0;
}

bool writeSaveFile(const std::string& path, const SavedGameState& state, const SaveSlotMetadata& metadata)
{
    if (!ensureParentDirectory(path))
        return false;

    nlohmann::json root;
    root["version"] = 10;
    root["saveMeta"] = saveMetadataToJson(metadata);
    root["sceneId"] = state.sceneId;
    root["activeSubSceneId"] = state.activeSubSceneId;
    root["previousSceneId"] = state.previousSceneId;
    root["previousSubSceneId"] = state.previousSubSceneId;
    root["narrativeText"] = state.narrativeText;
    root["health"] = state.health;
    root["energy"] = state.energy;
    root["resolve"] = state.resolve;
    root["lucidity"] = state.lucidity;
    root["charisma"] = state.charisma;
    root["walletCash"] = state.walletCash;
    root["hasSpokenInCurrentScene"] = state.hasSpokenInCurrentScene;
    root["hasUsedInCurrentScene"] = state.hasUsedInCurrentScene;
    root["examinedSceneIds"] = setToJsonArray(state.examinedSceneIds);
    root["usedSceneIds"] = setToJsonArray(state.usedSceneIds);
    root["takenItemKeys"] = setToJsonArray(state.takenItemKeys);
    root["usedInteractionKeys"] = setToJsonArray(state.usedInteractionKeys);
    root["storyFlags"] = setToJsonArray(state.storyFlags);
    root["consumedStatusActions"] = setToJsonArray(state.consumedStatusActions);
    root["committedPlayerDialogLines"] = setToJsonArray(state.committedPlayerDialogLines);
    nlohmann::json itemInstances = nlohmann::json::array();
    for (const InventoryItem& item : state.inventoryItems)
    {
        ItemInstance instance = item.instance;
        if (instance.defId.empty() && !item.id.empty())
            instance.defId = item.id;
        if (instance.defId.empty())
            continue;
        if (instance.instanceId.empty())
            instance.instanceId = instance.defId;
        itemInstances.push_back(itemInstanceToJson(instance));
    }
    root["itemInstances"] = itemInstances;
    root["droppedItems"] = droppedItemsToJson(state.droppedItemsByScene);
    root["sceneInventories"] = sceneInventoriesToJson(state.sceneInventories);
    root["conversation"] = {
        {"completedPhaseIds", setToJsonArray(state.conversation.completedPhaseIds)},
        {"completedRandomLineIds", setToJsonArray(state.conversation.completedRandomLineIds)},
        {"consumedScriptedChoiceIds", setToJsonArray(state.conversation.consumedScriptedChoiceIds)},
        {"persistedConsumedScriptedChoiceIds",
            setToJsonArray(state.conversation.persistedConsumedScriptedChoiceIds)},
        {"workTheRoomAttempts", actorOpinionsToJson(state.conversation.workTheRoomAttempts)}
    };
    root["milestones"] = milestonesToJson(state.milestones);
    root["day"] = state.day;
    root["actionCount"] = state.actionCount;
    root["saloonRoomPurchasedDay"] = state.saloonRoomPurchasedDay;
    root["lucidityCollapseCount"] = state.lucidityCollapseCount;
    root["lastLucidityCollapseDay"] = state.lastLucidityCollapseDay;
    root["lastSleepDay"] = state.lastSleepDay;
    root["flagGrantedDay"] = actorOpinionsToJson(state.flagGrantedDay);
    root["actorOpinions"] = actorOpinionsToJson(state.actorOpinions);
    root["actorTabOwed"] = actorTabOwedToJson(state.actorTabOwed);
    root["knownActorIds"] = setToJsonArray(state.knownActorIds);

    std::ofstream file(path.c_str());
    if (!file.is_open())
        return false;

    file << root.dump();
    return file.good();
}

bool readSaveFile(const std::string& path, SavedGameState& state, SaveSlotMetadata* outMetadata)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    if (!root.is_object())
        return false;

    if (outMetadata != nullptr)
    {
        const nlohmann::json& saveMeta = root.value("saveMeta", nlohmann::json::object());
        if (!saveMetadataFromJson(saveMeta, *outMetadata))
        {
            outMetadata->name = "Saved Game";
            outMetadata->timestampIso.clear();
            outMetadata->unixTime = 0;
            outMetadata->isQuickSave = false;
        }
    }

    state.sceneId = root.value("sceneId", "");
    state.activeSubSceneId = root.value("activeSubSceneId", "");
    state.previousSceneId = root.value("previousSceneId", "");
    state.previousSubSceneId = root.value("previousSubSceneId", "");
    state.narrativeText = root.value("narrativeText", "");
    state.health = root.value("health", state.health);
    state.energy = root.value("energy", state.energy);
    state.resolve = root.value("resolve", root.value("tenacity", state.resolve));
    state.lucidity = root.value("lucidity", state.lucidity);
    state.charisma = root.value("charisma", state.charisma);
    state.walletCash = root.value("walletCash", state.walletCash);
    state.hasSpokenInCurrentScene = root.value("hasSpokenInCurrentScene", false);
    state.hasUsedInCurrentScene = root.value("hasUsedInCurrentScene", false);

    jsonArrayToSet(root.value("examinedSceneIds", nlohmann::json::array()), state.examinedSceneIds);
    jsonArrayToSet(root.value("usedSceneIds", nlohmann::json::array()), state.usedSceneIds);
    jsonArrayToSet(root.value("takenItemKeys", nlohmann::json::array()), state.takenItemKeys);
    jsonArrayToSet(root.value("usedInteractionKeys", nlohmann::json::array()), state.usedInteractionKeys);
    jsonArrayToSet(root.value("storyFlags", nlohmann::json::array()), state.storyFlags);
    jsonArrayToSet(root.value("consumedStatusActions", nlohmann::json::array()), state.consumedStatusActions);
    jsonArrayToSet(root.value("committedPlayerDialogLines", nlohmann::json::array()), state.committedPlayerDialogLines);
    const int saveVersion = root.value("version", 4);

    state.day = root.value("day", state.day);
    state.actionCount = root.value("actionCount", state.actionCount);
    state.saloonRoomPurchasedDay = root.value("saloonRoomPurchasedDay", state.saloonRoomPurchasedDay);
    if (saveVersion >= 10)
    {
        state.lucidityCollapseCount = root.value("lucidityCollapseCount", 0);
        state.lastLucidityCollapseDay = root.value("lastLucidityCollapseDay", 0);
        state.lastSleepDay = root.value("lastSleepDay", 0);
        actorOpinionsFromJson(
            root.value("flagGrantedDay", nlohmann::json::object()),
            state.flagGrantedDay);
    }
    actorOpinionsFromJson(root.value("actorOpinions", nlohmann::json::object()), state.actorOpinions);
    actorTabOwedFromJson(root.value("actorTabOwed", nlohmann::json::object()), state.actorTabOwed);

    if (saveVersion >= 8)
        jsonArrayToSet(root.value("knownActorIds", nlohmann::json::array()), state.knownActorIds);
    if (saveVersion >= 5)
    {
        const nlohmann::json& itemInstances = root.value("itemInstances", nlohmann::json::array());
        const nlohmann::json& legacyInventory = root.value("inventoryItems", nlohmann::json::array());
        std::vector<InventoryItem> legacyItems;
        inventoryFromJson(legacyInventory, legacyItems);

        if (itemInstances.is_array() && !itemInstances.empty())
        {
            state.inventoryItems.clear();
            size_t legacyIndex = 0;
            for (const nlohmann::json& entry : itemInstances)
            {
                if (!entry.is_object())
                    continue;

                ItemInstance instance;
                itemInstanceFromJson(entry, instance);
                if (instance.defId.empty())
                    continue;

                InventoryItem item;
                if (legacyIndex < legacyItems.size()
                    && (legacyItems[legacyIndex].id == instance.defId
                        || legacyItems[legacyIndex].instance.defId == instance.defId))
                {
                    item = legacyItems[legacyIndex];
                    item.instance = instance;
                    item.id = item.id.empty() ? instance.defId : item.id;
                    ++legacyIndex;
                }
                else
                {
                    for (const InventoryItem& legacy : legacyItems)
                    {
                        if (legacy.id == instance.defId || legacy.instance.defId == instance.defId)
                        {
                            item = legacy;
                            item.instance = instance;
                            item.id = item.id.empty() ? instance.defId : item.id;
                            break;
                        }
                    }
                    if (item.id.empty())
                    {
                        item.instance = instance;
                        item.id = instance.defId;
                    }
                }

                state.inventoryItems.push_back(item);
            }

            if (state.inventoryItems.empty())
                state.inventoryItems = legacyItems;
        }
        else
        {
            state.inventoryItems = legacyItems;
        }
    }
    else
    {
        inventoryFromJson(root.value("inventoryItems", nlohmann::json::array()), state.inventoryItems);
    }
    droppedItemsFromJson(root.value("droppedItems", nlohmann::json::object()), state.droppedItemsByScene);
    if (saveVersion >= 9)
    {
        sceneInventoriesFromJson(
            root.value("sceneInventories", nlohmann::json::object()),
            state.sceneInventories);
    }

    const nlohmann::json& conversation = root.value("conversation", nlohmann::json::object());
    jsonArrayToSet(
        conversation.value("completedPhaseIds", nlohmann::json::array()),
        state.conversation.completedPhaseIds);
    jsonArrayToSet(
        conversation.value("completedRandomLineIds", nlohmann::json::array()),
        state.conversation.completedRandomLineIds);
    jsonArrayToSet(
        conversation.value("consumedScriptedChoiceIds", nlohmann::json::array()),
        state.conversation.consumedScriptedChoiceIds);
    jsonArrayToSet(
        conversation.value("persistedConsumedScriptedChoiceIds", nlohmann::json::array()),
        state.conversation.persistedConsumedScriptedChoiceIds);
    actorOpinionsFromJson(
        conversation.value("workTheRoomAttempts", nlohmann::json::object()),
        state.conversation.workTheRoomAttempts);
    milestonesFromJson(root.value("milestones", nlohmann::json::object()), state.milestones);

    return !state.sceneId.empty();
}

}