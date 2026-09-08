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

#include "SceneDocument.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace timberline_engine
{

namespace
{

std::string formatJsonValue(const nlohmann::json& value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    if (value.is_number_integer())
        return std::to_string(value.get<long long>());
    if (value.is_number_float())
    {
        std::ostringstream stream;
        stream << value.get<double>();
        return stream.str();
    }
    if (value.is_null())
        return "(null)";
    if (value.is_array())
        return "[array, " + std::to_string(value.size()) + " items]";
    if (value.is_object())
        return "{object, " + std::to_string(value.size()) + " keys}";
    return value.dump();
}

bool isLayoutKey(const std::string& key)
{
    return key == "layout";
}

bool isActorKey(const std::string& key)
{
    return key == "actors";
}

}

bool SceneDocument::load(const std::string& path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json parsed;
    try
    {
        file >> parsed;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    if (!parsed.is_object() || !parsed.contains("scenes") || !parsed["scenes"].is_object())
        return false;

    root = parsed;
    filePath = path;
    return true;
}

bool SceneDocument::save(const std::string& path) const
{
    if (!isLoaded())
        return false;

    std::ofstream out(path.c_str());
    if (!out.is_open())
        return false;

    out << root.dump(2);
    return out.good();
}

bool SceneDocument::save() const
{
    if (filePath.empty())
        return false;
    return save(filePath);
}

std::vector<std::string> SceneDocument::sceneIds() const
{
    std::vector<std::string> ids;
    if (!isLoaded())
        return ids;

    const nlohmann::json& scenes = root["scenes"];
    ids.reserve(scenes.size());
    for (auto it = scenes.begin(); it != scenes.end(); ++it)
        ids.push_back(it.key());

    std::sort(ids.begin(), ids.end());
    return ids;
}

void SceneDocument::parseMapNodeId(
    const std::string& mapNodeId,
    std::string& outParentId,
    std::string& outSubSceneId)
{
    outParentId.clear();
    outSubSceneId.clear();
    if (mapNodeId.empty())
        return;
    const size_t hashPos = mapNodeId.find('#');
    if (hashPos == std::string::npos)
    {
        outParentId = mapNodeId;
        return;
    }
    outParentId = mapNodeId.substr(0, hashPos);
    outSubSceneId = mapNodeId.substr(hashPos + 1);
}

std::string SceneDocument::makeMapNodeId(
    const std::string& parentId,
    const std::string& subSceneId)
{
    if (parentId.empty())
        return {};
    if (subSceneId.empty())
        return parentId;
    return parentId + "#" + subSceneId;
}

bool SceneDocument::hasScene(const std::string& sceneId) const
{
    return isLoaded() && root["scenes"].contains(sceneId);
}

bool SceneDocument::hasMapNode(const std::string& mapNodeId) const
{
    std::string parentId;
    std::string subId;
    parseMapNodeId(mapNodeId, parentId, subId);
    if (!hasScene(parentId))
        return false;
    if (subId.empty())
        return true;
    const nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr || !parent->contains("subScenes") || !(*parent)["subScenes"].is_object())
        return false;
    return (*parent)["subScenes"].contains(subId)
        && (*parent)["subScenes"][subId].is_object();
}

std::vector<std::string> SceneDocument::subSceneIds(const std::string& parentId) const
{
    std::vector<std::string> ids;
    const nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr || !parent->contains("subScenes") || !(*parent)["subScenes"].is_object())
        return ids;
    for (auto it = (*parent)["subScenes"].begin(); it != (*parent)["subScenes"].end(); ++it)
    {
        if (it.value().is_object())
            ids.push_back(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> SceneDocument::mapNodeIds() const
{
    std::vector<std::string> ids;
    if (!isLoaded())
        return ids;

    for (const std::string& parentId : sceneIds())
    {
        if (hasMapPlacement(parentId))
            ids.push_back(parentId);
        for (const std::string& subId : subSceneIds(parentId))
        {
            const std::string nodeId = makeMapNodeId(parentId, subId);
            if (hasMapPlacement(nodeId))
                ids.push_back(nodeId);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool SceneDocument::hasMapPlacement(const std::string& mapNodeId) const
{
    std::string parentId;
    std::string subId;
    parseMapNodeId(mapNodeId, parentId, subId);
    const nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr)
        return false;
    if (subId.empty())
        return parent->contains("layout") && (*parent)["layout"].is_object();
    if (!parent->contains("subScenes") || !(*parent)["subScenes"].is_object())
        return false;
    if (!(*parent)["subScenes"].contains(subId) || !(*parent)["subScenes"][subId].is_object())
        return false;
    const nlohmann::json& sub = (*parent)["subScenes"][subId];
    return sub.contains("layout") && sub["layout"].is_object();
}

bool SceneDocument::createScene(const std::string& sceneId, const nlohmann::json& sceneObject)
{
    if (!isLoaded() || sceneId.empty() || hasScene(sceneId) || !sceneObject.is_object())
        return false;
    root["scenes"][sceneId] = sceneObject;
    return true;
}

std::string SceneDocument::allocateUniqueSceneId(const std::string& baseId) const
{
    if (baseId.empty())
        return {};
    if (!hasScene(baseId))
        return baseId;

    for (int n = 2; n < 10000; ++n)
    {
        const std::string candidate = baseId + "_" + std::to_string(n);
        if (!hasScene(candidate))
            return candidate;
    }
    return {};
}

std::string SceneDocument::duplicateScene(const std::string& sourceId)
{
    if (!hasScene(sourceId))
        return {};

    const std::string newId = allocateUniqueSceneId(sourceId);
    if (newId.empty() || newId == sourceId)
        return {};

    const nlohmann::json* source = sceneJson(sourceId);
    if (source == nullptr || !source->is_object())
        return {};

    nlohmann::json copy = *source;
    // Independent room: no map pose and no outbound graph until the author links it.
    copy.erase("layout");
    if (copy.contains("exits") && copy["exits"].is_object())
        copy["exits"] = nlohmann::json::object();
    if (copy.contains("movement") && copy["movement"].is_object())
    {
        for (auto it = copy["movement"].begin(); it != copy["movement"].end(); ++it)
        {
            if (it.value().is_boolean())
                it.value() = false;
        }
    }
    if (copy.contains("exitRequirements") && copy["exitRequirements"].is_object())
        copy["exitRequirements"] = nlohmann::json::object();
    if (copy.contains("movementExits") && copy["movementExits"].is_object())
        copy["movementExits"] = nlohmann::json::object();
    copy["start"] = false;

    if (!createScene(newId, copy))
        return {};
    return newId;
}

bool SceneDocument::removeScene(const std::string& sceneId)
{
    if (!hasScene(sceneId))
        return false;

    nlohmann::json& scenes = root["scenes"];
    for (auto it = scenes.begin(); it != scenes.end(); ++it)
    {
        if (it.key() == sceneId || !it.value().is_object())
            continue;

        nlohmann::json& scene = it.value();
        if (!scene.contains("exits") || !scene["exits"].is_object())
            continue;

        std::vector<std::string> directionsToClear;
        for (auto exitIt = scene["exits"].begin(); exitIt != scene["exits"].end(); ++exitIt)
        {
            if (exitIt.value().is_string()
                && exitIt.value().get<std::string>() == sceneId)
            {
                directionsToClear.push_back(exitIt.key());
            }
        }

        for (const std::string& direction : directionsToClear)
        {
            scene["exits"].erase(direction);
            if (scene.contains("movement") && scene["movement"].is_object())
                scene["movement"][direction] = false;
            if (scene.contains("exitRequirements") && scene["exitRequirements"].is_object())
                scene["exitRequirements"].erase(direction);
        }
    }

    scenes.erase(sceneId);
    return true;
}

bool SceneDocument::renameScene(const std::string& oldId, const std::string& newId)
{
    if (!isLoaded() || oldId.empty() || newId.empty() || oldId == newId)
        return false;
    if (!hasScene(oldId) || hasScene(newId))
        return false;

    nlohmann::json& scenes = root["scenes"];
    nlohmann::json moved = scenes[oldId];
    scenes.erase(oldId);
    scenes[newId] = std::move(moved);

    // Retarget inbound exit links that pointed at the old id.
    for (auto it = scenes.begin(); it != scenes.end(); ++it)
    {
        if (!it.value().is_object())
            continue;
        nlohmann::json& scene = it.value();
        if (!scene.contains("exits") || !scene["exits"].is_object())
            continue;
        for (auto exitIt = scene["exits"].begin(); exitIt != scene["exits"].end(); ++exitIt)
        {
            if (exitIt.value().is_string()
                && exitIt.value().get<std::string>() == oldId)
                exitIt.value() = newId;
        }
    }
    return true;
}

SceneLayout SceneDocument::getLayout(const std::string& mapNodeId) const
{
    SceneLayout layout;
    std::string parentId;
    std::string subId;
    parseMapNodeId(mapNodeId, parentId, subId);
    const nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr)
        return layout;

    const nlohmann::json* layoutOwner = parent;
    if (!subId.empty())
    {
        if (!parent->contains("subScenes") || !(*parent)["subScenes"].is_object())
            return layout;
        if (!(*parent)["subScenes"].contains(subId) || !(*parent)["subScenes"][subId].is_object())
            return layout;
        layoutOwner = &(*parent)["subScenes"][subId];
    }
    if (layoutOwner == nullptr || !layoutOwner->contains("layout")
        || !(*layoutOwner)["layout"].is_object())
        return layout;

    const nlohmann::json& layoutJson = (*layoutOwner)["layout"];
    layout.x = layoutJson.value("x", 0.0f);
    layout.y = layoutJson.value("y", 0.0f);
    layout.level = layoutJson.value("level", 0);
    return layout;
}

void SceneDocument::setLayout(const std::string& mapNodeId, const SceneLayout& layout)
{
    std::string parentId;
    std::string subId;
    parseMapNodeId(mapNodeId, parentId, subId);
    nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr)
        return;

    nlohmann::json layoutJson = {
        {"x", layout.x},
        {"y", layout.y},
        {"level", layout.level}
    };

    if (subId.empty())
    {
        (*parent)["layout"] = layoutJson;
        return;
    }

    if (!parent->contains("subScenes") || !(*parent)["subScenes"].is_object())
        (*parent)["subScenes"] = nlohmann::json::object();
    if (!(*parent)["subScenes"].contains(subId) || !(*parent)["subScenes"][subId].is_object())
        (*parent)["subScenes"][subId] = nlohmann::json::object();
    (*parent)["subScenes"][subId]["layout"] = layoutJson;
}

void SceneDocument::clearLayout(const std::string& mapNodeId)
{
    std::string parentId;
    std::string subId;
    parseMapNodeId(mapNodeId, parentId, subId);
    nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr)
        return;
    if (subId.empty())
    {
        parent->erase("layout");
        return;
    }
    if (!parent->contains("subScenes") || !(*parent)["subScenes"].is_object())
        return;
    if (!(*parent)["subScenes"].contains(subId) || !(*parent)["subScenes"][subId].is_object())
        return;
    (*parent)["subScenes"][subId].erase("layout");
}

std::vector<SceneActor> SceneDocument::getActors(const std::string& sceneId) const
{
    std::vector<SceneActor> actors;
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr || !scene->contains("actors") || !(*scene)["actors"].is_array())
        return actors;

    for (const nlohmann::json& actorJson : (*scene)["actors"])
    {
        if (!actorJson.is_object())
            continue;

        SceneActor actor;
        actor.id = actorJson.value("id", "");
        actor.name = actorJson.value("name", "");
        actor.role = actorJson.value("role", "");
        actor.x = actorJson.value("x", 0.0f);
        actor.y = actorJson.value("y", 0.0f);
        if (!actor.id.empty())
            actors.push_back(actor);
    }

    return actors;
}

std::string SceneDocument::getSceneImagePath(const std::string& mapNodeId) const
{
    std::string parentId;
    std::string subId;
    parseMapNodeId(mapNodeId, parentId, subId);
    const nlohmann::json* parent = sceneJson(parentId);
    if (parent == nullptr)
        return "";

    if (!subId.empty()
        && parent->contains("subScenes")
        && (*parent)["subScenes"].is_object()
        && (*parent)["subScenes"].contains(subId)
        && (*parent)["subScenes"][subId].is_object())
    {
        const std::string subImage =
            (*parent)["subScenes"][subId].value("image", "");
        if (!subImage.empty())
            return subImage;
    }

    // Prefer authored 16x9 master when present (editor thumbs / map cards).
    if (parent->contains("imageVariants") && (*parent)["imageVariants"].is_object())
    {
        const auto& variants = (*parent)["imageVariants"];
        if (variants.contains("16x9") && variants["16x9"].is_string())
        {
            const std::string v = variants["16x9"].get<std::string>();
            if (!v.empty())
                return v;
        }
    }
    return parent->value("image", "");
}

std::string SceneDocument::getSceneMusicPath(const std::string& sceneId) const
{
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return "";
    const nlohmann::json audio = scene->value("audio", nlohmann::json::object());
    if (!audio.is_object())
        return "";
    if (audio.contains("music") && audio["music"].is_object())
        return audio["music"].value("path", "");
    if (audio.contains("music") && audio["music"].is_string())
        return audio["music"].get<std::string>();
    return "";
}

std::string SceneDocument::getSceneAmbientPath(const std::string& sceneId) const
{
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return "";
    const nlohmann::json audio = scene->value("audio", nlohmann::json::object());
    if (!audio.is_object() || !audio.contains("ambient"))
        return "";
    const nlohmann::json& ambient = audio["ambient"];
    if (ambient.is_array() && !ambient.empty())
    {
        if (ambient[0].is_object())
            return ambient[0].value("path", "");
        if (ambient[0].is_string())
            return ambient[0].get<std::string>();
    }
    if (ambient.is_object())
        return ambient.value("path", "");
    if (ambient.is_string())
        return ambient.get<std::string>();
    return "";
}

nlohmann::json* SceneDocument::sceneJson(const std::string& sceneId)
{
    if (!isLoaded() || !root["scenes"].contains(sceneId))
        return nullptr;
    return &root["scenes"][sceneId];
}

const nlohmann::json* SceneDocument::sceneJson(const std::string& sceneId) const
{
    if (!isLoaded() || !root["scenes"].contains(sceneId))
        return nullptr;
    return &root["scenes"][sceneId];
}

std::vector<std::pair<std::string, std::string>> SceneDocument::sceneVariableRows(
    const std::string& sceneId) const
{
    std::vector<std::pair<std::string, std::string>> rows;
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return rows;

    std::vector<std::string> keys;
    keys.reserve(scene->size());
    for (auto it = scene->begin(); it != scene->end(); ++it)
    {
        if (isLayoutKey(it.key()) || isActorKey(it.key()))
            continue;
        keys.push_back(it.key());
    }

    std::sort(keys.begin(), keys.end());
    rows.reserve(keys.size());
    for (const std::string& key : keys)
        rows.push_back({ key, formatJsonValue((*scene)[key]) });

    return rows;
}

}