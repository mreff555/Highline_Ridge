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

#include "DocumentWorkspace.h"

#include "EditorPaths.h"
#include "PlatformPath.h"

#include <algorithm>
#include <fstream>

using timberline_engine::listDirectoryFileNames;
using timberline_engine::pathJoin;

namespace timberline_editor
{

std::vector<std::string> DocumentWorkspace::listJsonResources() const
{
    std::vector<std::string> files;
    if (!resourceDirectoryExists(resourceDir))
        return files;

    const std::vector<std::string> names = listDirectoryFileNames(resourceDir);
    for (const std::string& name : names)
    {
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0)
            continue;
        // Craft recipes live on product items; do not offer a combinations tab.
        if (name == "item_combinations.json")
            continue;
        // Editor only implements these three resources.
        if (name != "scenes.json"
            && name != "conversations.json"
            && name != "items.json")
            continue;
        files.push_back(name);
    }

    std::sort(files.begin(), files.end());
    return files;
}

void DocumentWorkspace::refreshTabs()
{
    jsonTabs = listJsonResources();
    if (jsonTabs.empty())
    {
        activeTabIndex = 0;
        return;
    }

    int scenesIndex = -1;
    for (size_t i = 0; i < jsonTabs.size(); ++i)
    {
        if (jsonTabs[i] == "scenes.json")
        {
            scenesIndex = static_cast<int>(i);
            break;
        }
    }

    if (scenesIndex >= 0)
        activeTabIndex = scenesIndex;
    else if (activeTabIndex >= static_cast<int>(jsonTabs.size()))
        activeTabIndex = 0;
}

std::string DocumentWorkspace::activeTabFilename() const
{
    if (activeTabIndex < 0 || activeTabIndex >= static_cast<int>(jsonTabs.size()))
        return "";
    return jsonTabs[static_cast<size_t>(activeTabIndex)];
}

bool DocumentWorkspace::isConversationsTab() const
{
    return activeTabFilename() == "conversations.json";
}

bool DocumentWorkspace::isScenesTab() const
{
    return activeTabFilename() == "scenes.json";
}

bool DocumentWorkspace::isItemsTab() const
{
    return activeTabFilename() == "items.json";
}

bool DocumentWorkspace::loadScenesFile()
{
    const std::string scenesPath = pathJoin(resourceDir, "scenes.json");
    if (!scenes.load(scenesPath))
    {
        loadError = "Failed to load scenes.json:\n" + scenesPath;
        scenes = timberline_engine::SceneDocument{};
        return false;
    }
    return true;
}

bool DocumentWorkspace::loadConversationsDocument()
{
    conversationsLoaded = false;
    conversationsRoot = nlohmann::json::object();
    conversationsPath = pathJoin(resourceDir, "conversations.json");

    std::ifstream file(conversationsPath.c_str());
    if (!file.is_open())
    {
        loadError = "Failed to open conversations.json:\n" + conversationsPath;
        return false;
    }

    nlohmann::json parsed;
    try
    {
        file >> parsed;
    }
    catch (const nlohmann::json::exception& ex)
    {
        loadError = std::string("Failed to parse conversations.json:\n") + ex.what();
        return false;
    }

    if (!parsed.is_object())
    {
        loadError = "conversations.json root must be an object.";
        return false;
    }

    conversationsRoot = std::move(parsed);
    conversationsLoaded = true;
    return true;
}

bool DocumentWorkspace::saveConversationsDocument()
{
    if (!conversationsLoaded || conversationsPath.empty())
        return false;

    std::ofstream out(conversationsPath.c_str());
    if (!out.is_open())
        return false;

    out << conversationsRoot.dump(2) << '\n';
    return out.good();
}

bool DocumentWorkspace::loadItemsDocument()
{
    itemsLoaded = false;
    itemsRoot = nlohmann::json::object();
    itemsPath = pathJoin(resourceDir, "items.json");

    std::ifstream file(itemsPath.c_str());
    if (!file.is_open())
    {
        loadError = "Failed to open items.json:\n" + itemsPath;
        return false;
    }

    nlohmann::json parsed;
    try
    {
        file >> parsed;
    }
    catch (const nlohmann::json::exception& ex)
    {
        loadError = std::string("Failed to parse items.json:\n") + ex.what();
        return false;
    }

    if (!parsed.is_object())
    {
        loadError = "items.json root must be an object.";
        return false;
    }

    // Normalize to { "items": { id: {...}, ... } } so saves stay consistent.
    if (!parsed.contains("items") || !parsed["items"].is_object())
    {
        nlohmann::json wrapped = nlohmann::json::object();
        wrapped["items"] = parsed;
        itemsRoot = std::move(wrapped);
    }
    else
    {
        itemsRoot = std::move(parsed);
    }

    itemsLoaded = true;
    return true;
}

bool DocumentWorkspace::saveItemsDocument()
{
    if (!itemsLoaded || itemsPath.empty())
        return false;

    std::ofstream out(itemsPath.c_str());
    if (!out.is_open())
        return false;

    out << itemsRoot.dump(2) << '\n';
    return out.good();
}

bool DocumentWorkspace::saveAll()
{
    bool ok = true;
    if (conversationsLoaded)
        ok = saveConversationsDocument() && ok;
    if (itemsLoaded)
        ok = saveItemsDocument() && ok;
    if (scenes.isLoaded())
        ok = scenes.save() && ok;
    if (ok)
        dirty = false;
    return ok && (scenes.isLoaded() || conversationsLoaded || itemsLoaded);
}

void DocumentWorkspace::markDirty()
{
    dirty = true;
}

nlohmann::json* DocumentWorkspace::conversationJsonAt(const std::string& pointer)
{
    if (!conversationsLoaded || pointer.empty())
        return nullptr;
    try
    {
        return &conversationsRoot.at(nlohmann::json::json_pointer(pointer));
    }
    catch (const nlohmann::json::exception&)
    {
        return nullptr;
    }
}

const nlohmann::json* DocumentWorkspace::conversationJsonAt(const std::string& pointer) const
{
    if (!conversationsLoaded || pointer.empty())
        return nullptr;
    try
    {
        return &conversationsRoot.at(nlohmann::json::json_pointer(pointer));
    }
    catch (const nlohmann::json::exception&)
    {
        return nullptr;
    }
}

nlohmann::json* DocumentWorkspace::sceneFieldAt(
    const std::string& sceneId,
    const std::string& pointerUnderScene)
{
    nlohmann::json* scene = scenes.sceneJson(sceneId);
    if (scene == nullptr || pointerUnderScene.empty())
        return nullptr;
    try
    {
        return &scene->at(nlohmann::json::json_pointer(pointerUnderScene));
    }
    catch (const nlohmann::json::exception&)
    {
        return nullptr;
    }
}

const nlohmann::json* DocumentWorkspace::sceneFieldAt(
    const std::string& sceneId,
    const std::string& pointerUnderScene) const
{
    const nlohmann::json* scene = scenes.sceneJson(sceneId);
    if (scene == nullptr || pointerUnderScene.empty())
        return nullptr;
    try
    {
        return &scene->at(nlohmann::json::json_pointer(pointerUnderScene));
    }
    catch (const nlohmann::json::exception&)
    {
        return nullptr;
    }
}

void DocumentWorkspace::clearScenes()
{
    scenes = timberline_engine::SceneDocument{};
}

void DocumentWorkspace::clearConversations()
{
    conversationsLoaded = false;
    conversationsRoot = nlohmann::json::object();
    conversationsPath.clear();
}

void DocumentWorkspace::clearItems()
{
    itemsLoaded = false;
    itemsRoot = nlohmann::json::object();
    itemsPath.clear();
}

nlohmann::json* DocumentWorkspace::itemsMap()
{
    if (!itemsLoaded || !itemsRoot.is_object())
        return nullptr;
    if (!itemsRoot.contains("items") || !itemsRoot["items"].is_object())
        return nullptr;
    return &itemsRoot["items"];
}

const nlohmann::json* DocumentWorkspace::itemsMap() const
{
    if (!itemsLoaded || !itemsRoot.is_object())
        return nullptr;
    if (!itemsRoot.contains("items") || !itemsRoot["items"].is_object())
        return nullptr;
    return &itemsRoot["items"];
}

nlohmann::json* DocumentWorkspace::itemJson(const std::string& itemId)
{
    nlohmann::json* map = itemsMap();
    if (map == nullptr || itemId.empty() || !map->contains(itemId))
        return nullptr;
    nlohmann::json& item = (*map)[itemId];
    return item.is_object() ? &item : nullptr;
}

const nlohmann::json* DocumentWorkspace::itemJson(const std::string& itemId) const
{
    const nlohmann::json* map = itemsMap();
    if (map == nullptr || itemId.empty() || !map->contains(itemId))
        return nullptr;
    const nlohmann::json& item = (*map)[itemId];
    return item.is_object() ? &item : nullptr;
}

nlohmann::json* DocumentWorkspace::itemFieldAt(
    const std::string& itemId,
    const std::string& pointerUnderItem)
{
    nlohmann::json* item = itemJson(itemId);
    if (item == nullptr || pointerUnderItem.empty())
        return nullptr;
    try
    {
        return &item->at(nlohmann::json::json_pointer(pointerUnderItem));
    }
    catch (const nlohmann::json::exception&)
    {
        return nullptr;
    }
}

const nlohmann::json* DocumentWorkspace::itemFieldAt(
    const std::string& itemId,
    const std::string& pointerUnderItem) const
{
    const nlohmann::json* item = itemJson(itemId);
    if (item == nullptr || pointerUnderItem.empty())
        return nullptr;
    try
    {
        return &item->at(nlohmann::json::json_pointer(pointerUnderItem));
    }
    catch (const nlohmann::json::exception&)
    {
        return nullptr;
    }
}

std::vector<std::string> DocumentWorkspace::itemIds() const
{
    std::vector<std::string> ids;
    const nlohmann::json* map = itemsMap();
    if (map == nullptr)
        return ids;
    ids.reserve(map->size());
    for (auto it = map->begin(); it != map->end(); ++it)
        ids.push_back(it.key());
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace timberline_editor
