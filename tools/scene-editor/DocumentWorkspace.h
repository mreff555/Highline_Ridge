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

#ifndef TIMBERLINE_DOCUMENT_WORKSPACE_H
#define TIMBERLINE_DOCUMENT_WORKSPACE_H

#include "SceneDocument.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace timberline_editor
{

// Owns scene/conversation JSON, tabs, and dirty state for the resource editor.
struct DocumentWorkspace
{
    std::string resourceDir = "../../../resources";
    std::string assetRoot = "../../..";
    std::string loadError;

    timberline_engine::SceneDocument scenes;
    nlohmann::json conversationsRoot = nlohmann::json::object();
    std::string conversationsPath;
    bool conversationsLoaded = false;

    /** Root of items.json (object with "items" map, or flat id→item map). */
    nlohmann::json itemsRoot = nlohmann::json::object();
    std::string itemsPath;
    bool itemsLoaded = false;

    std::vector<std::string> jsonTabs;
    int activeTabIndex = 0;
    bool dirty = false;

    std::vector<std::string> listJsonResources() const;
    void refreshTabs();
    std::string activeTabFilename() const;
    bool isConversationsTab() const;
    bool isScenesTab() const;
    bool isItemsTab() const;

    bool loadScenesFile();
    bool loadConversationsDocument();
    bool saveConversationsDocument();
    bool loadItemsDocument();
    bool saveItemsDocument();
    bool saveAll();
    void markDirty();

    nlohmann::json* conversationJsonAt(const std::string& pointer);
    const nlohmann::json* conversationJsonAt(const std::string& pointer) const;
    nlohmann::json* sceneFieldAt(const std::string& sceneId, const std::string& pointerUnderScene);
    const nlohmann::json* sceneFieldAt(const std::string& sceneId, const std::string& pointerUnderScene) const;

    /** Mutable map of item id → item object. */
    nlohmann::json* itemsMap();
    const nlohmann::json* itemsMap() const;
    nlohmann::json* itemJson(const std::string& itemId);
    const nlohmann::json* itemJson(const std::string& itemId) const;
    nlohmann::json* itemFieldAt(const std::string& itemId, const std::string& pointerUnderItem);
    const nlohmann::json* itemFieldAt(const std::string& itemId, const std::string& pointerUnderItem) const;
    std::vector<std::string> itemIds() const;

    void clearScenes();
    void clearConversations();
    void clearItems();
};

} // namespace timberline_editor

#endif /* TIMBERLINE_DOCUMENT_WORKSPACE_H */
