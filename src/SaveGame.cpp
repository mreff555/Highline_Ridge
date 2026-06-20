#include "SaveGame.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sys/stat.h>

namespace testgame
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
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return true;

    const std::string directory = path.substr(0, slash);
    struct stat info{};
    if (stat(directory.c_str(), &info) == 0)
        return S_ISDIR(info.st_mode);

    if (mkdir(directory.c_str(), 0755) != 0)
        return stat(directory.c_str(), &info) == 0 && S_ISDIR(info.st_mode);

    return true;
}

nlohmann::json inventoryToJson(const std::vector<InventoryItem>& items)
{
    nlohmann::json array = nlohmann::json::array();
    for (const InventoryItem& item : items)
    {
        if (item.id.empty())
            continue;

        array.push_back({
            {"id", item.id},
            {"name", item.name},
            {"iconPath", item.iconPath},
            {"examineImagePath", item.examineImagePath},
            {"examineText", item.examineText}
        });
    }
    return array;
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
        if (!item.id.empty())
            outItems.push_back(item);
    }
}

}

const char* defaultSavePath()
{
    return "saves/highline_ridge.sav";
}

bool saveFileExists(const std::string& path)
{
    return std::ifstream(path.c_str()).good();
}

bool writeSaveFile(const std::string& path, const SavedGameState& state)
{
    if (!ensureParentDirectory(path))
        return false;

    nlohmann::json root;
    root["version"] = 1;
    root["sceneId"] = state.sceneId;
    root["previousSceneId"] = state.previousSceneId;
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
    root["storyFlags"] = setToJsonArray(state.storyFlags);
    root["consumedStatusActions"] = setToJsonArray(state.consumedStatusActions);
    root["committedPlayerDialogLines"] = setToJsonArray(state.committedPlayerDialogLines);
    root["inventoryItems"] = inventoryToJson(state.inventoryItems);
    root["conversation"] = {
        {"completedPhaseIds", setToJsonArray(state.conversation.completedPhaseIds)},
        {"completedRandomLineIds", setToJsonArray(state.conversation.completedRandomLineIds)}
    };

    std::ofstream file(path.c_str());
    if (!file.is_open())
        return false;

    file << root.dump(2);
    return file.good();
}

bool readSaveFile(const std::string& path, SavedGameState& state)
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

    state.sceneId = root.value("sceneId", "");
    state.previousSceneId = root.value("previousSceneId", "");
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
    jsonArrayToSet(root.value("storyFlags", nlohmann::json::array()), state.storyFlags);
    jsonArrayToSet(root.value("consumedStatusActions", nlohmann::json::array()), state.consumedStatusActions);
    jsonArrayToSet(root.value("committedPlayerDialogLines", nlohmann::json::array()), state.committedPlayerDialogLines);
    inventoryFromJson(root.value("inventoryItems", nlohmann::json::array()), state.inventoryItems);

    const nlohmann::json& conversation = root.value("conversation", nlohmann::json::object());
    jsonArrayToSet(
        conversation.value("completedPhaseIds", nlohmann::json::array()),
        state.conversation.completedPhaseIds);
    jsonArrayToSet(
        conversation.value("completedRandomLineIds", nlohmann::json::array()),
        state.conversation.completedRandomLineIds);

    return !state.sceneId.empty();
}

}