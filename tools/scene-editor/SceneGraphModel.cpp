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

#include "SceneGraphModel.h"
#include "SceneMapCanvas.h"

#include "ConversationHelpers.h"
#include "DocumentWorkspace.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "RaylibCompat.h"
#include "SceneDocument.h"
#include "ThumbnailCache.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <filesystem>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using timberline_engine::SceneActor;
using timberline_engine::SceneDocument;
using timberline_engine::SceneLayout;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::listDirectoryFileNames;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace fs = std::filesystem;

namespace timberline_editor
{
#include "SceneMapCanvas.h"


std::string SceneGraphModel::getExitTarget(const std::string& sceneId, const std::string& direction) const
{
    const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->contains("exits") || !(*scene)["exits"].is_object())
        return "";
    if (!(*scene)["exits"].contains(direction) || !(*scene)["exits"][direction].is_string())
        return "";
    return (*scene)["exits"][direction].get<std::string>();
}


void SceneGraphModel::setExitTarget(const std::string& sceneId, const std::string& direction, const std::string& targetId)
{
    nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr)
        return;

    if (!scene->contains("exits") || !(*scene)["exits"].is_object())
        (*scene)["exits"] = nlohmann::json::object();
    (*scene)["exits"][direction] = targetId;

    if (!scene->contains("movement") || !(*scene)["movement"].is_object())
        (*scene)["movement"] = nlohmann::json::object();
    (*scene)["movement"][direction] = true;
}


void SceneGraphModel::clearExitTarget(const std::string& sceneId, const std::string& direction)
{
    nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr)
        return;

    if (scene->contains("exits") && (*scene)["exits"].is_object())
        (*scene)["exits"].erase(direction);

    if (scene->contains("movement") && (*scene)["movement"].is_object())
        (*scene)["movement"][direction] = false;

    // Direction-scoped requirements no longer apply without an exit.
    if (scene->contains("exitRequirements") && (*scene)["exitRequirements"].is_object())
        (*scene)["exitRequirements"].erase(direction);
}


bool SceneGraphModel::deleteExitLink(
    const std::string& fromId,
    const std::string& direction,
    bool clearReciprocal)
{
    if (!docs || !docs->scenes.isLoaded())
        return false;
    if (fromId.empty() || direction.empty() || !docs->scenes.hasScene(fromId))
        return false;

    const std::string toId = getExitTarget(fromId, direction);
    if (toId.empty())
        return false;

    const std::string reverseDir = oppositeDirection(direction);
    const bool hadReciprocal =
        clearReciprocal
        && !reverseDir.empty()
        && getExitTarget(toId, reverseDir) == fromId;

    clearExitTarget(fromId, direction);
    if (hadReciprocal)
        clearExitTarget(toId, reverseDir);

    docs->markDirty();
    return true;
}


namespace
{

std::string sfxClipPath(const nlohmann::json& entry)
{
    if (entry.contains("path") && entry["path"].is_string())
        return entry["path"].get<std::string>();
    if (entry.contains("file") && entry["file"].is_string())
        return entry["file"].get<std::string>();
    return {};
}

std::string sfxClipTrigger(const nlohmann::json& entry)
{
    if (entry.contains("trigger") && entry["trigger"].is_string())
        return entry["trigger"].get<std::string>();
    return "on_enter";
}

bool isConstrainedEnter(const nlohmann::json& entry, const std::string& neighborId)
{
    if (sfxClipTrigger(entry) != "on_enter")
        return false;
    if (!entry.contains("from_room") || !entry["from_room"].is_string())
        return false;
    return entry["from_room"].get<std::string>() == neighborId;
}

bool isConstrainedExit(const nlohmann::json& entry, const std::string& neighborId)
{
    if (sfxClipTrigger(entry) != "on_exit")
        return false;
    if (!entry.contains("to_room") || !entry["to_room"].is_string())
        return false;
    return entry["to_room"].get<std::string>() == neighborId;
}

nlohmann::json* ensureSfxArray(nlohmann::json& scene)
{
    if (!scene.contains("audio") || !scene["audio"].is_object())
        scene["audio"] = nlohmann::json::object();
    nlohmann::json& audio = scene["audio"];
    if (!audio.contains("sfx") || !audio["sfx"].is_array())
        audio["sfx"] = nlohmann::json::array();
    return &audio["sfx"];
}

} // namespace


int SceneGraphModel::countConstrainedTransitionSfx(
    const std::string& sceneId,
    const std::string& neighborId) const
{
    const nlohmann::json* scene = docs ? docs->scenes.sceneJson(sceneId) : nullptr;
    if (scene == nullptr || neighborId.empty())
        return 0;
    if (!scene->contains("audio") || !(*scene)["audio"].is_object())
        return 0;
    const auto& audio = (*scene)["audio"];
    if (!audio.contains("sfx") || !audio["sfx"].is_array())
        return 0;
    int count = 0;
    for (const auto& entry : audio["sfx"])
    {
        if (!entry.is_object())
            continue;
        if (isConstrainedEnter(entry, neighborId) || isConstrainedExit(entry, neighborId))
            ++count;
    }
    return count;
}


std::string SceneGraphModel::preferTransitionSfxOwner(
    const std::string& sceneA,
    const std::string& sceneB,
    const std::string& preferDefaultOwner) const
{
    const int countA = countConstrainedTransitionSfx(sceneA, sceneB);
    const int countB = countConstrainedTransitionSfx(sceneB, sceneA);
    if (countA > countB)
        return sceneA;
    if (countB > countA)
        return sceneB;
    if (!preferDefaultOwner.empty()
        && (preferDefaultOwner == sceneA || preferDefaultOwner == sceneB))
        return preferDefaultOwner;
    return sceneB.empty() ? sceneA : sceneB;
}


SceneGraphModel::TransitionSfxPaths SceneGraphModel::readConstrainedTransitionSfx(
    const std::string& ownerId,
    const std::string& neighborId) const
{
    TransitionSfxPaths out;
    const nlohmann::json* scene = docs ? docs->scenes.sceneJson(ownerId) : nullptr;
    if (scene == nullptr || neighborId.empty())
        return out;
    if (!scene->contains("audio") || !(*scene)["audio"].is_object())
        return out;
    const auto& audio = (*scene)["audio"];
    if (!audio.contains("sfx") || !audio["sfx"].is_array())
        return out;
    for (const auto& entry : audio["sfx"])
    {
        if (!entry.is_object())
            continue;
        if (out.enterPath.empty() && isConstrainedEnter(entry, neighborId))
            out.enterPath = sfxClipPath(entry);
        if (out.exitPath.empty() && isConstrainedExit(entry, neighborId))
            out.exitPath = sfxClipPath(entry);
    }
    return out;
}


bool SceneGraphModel::upsertConstrainedTransitionSfx(
    const std::string& ownerId,
    const std::string& neighborId,
    const std::string& enterPath,
    const std::string& exitPath)
{
    if (!docs || !docs->scenes.isLoaded())
        return false;
    nlohmann::json* scene = docs->scenes.sceneJson(ownerId);
    if (scene == nullptr || neighborId.empty() || ownerId == neighborId)
        return false;

    nlohmann::json* sfx = ensureSfxArray(*scene);
    if (sfx == nullptr)
        return false;

    auto upsertOne = [&](bool isEnter, const std::string& path) {
        const char* roomKey = isEnter ? "from_room" : "to_room";
        const char* trigger = isEnter ? "on_enter" : "on_exit";
        int found = -1;
        for (int i = 0; i < static_cast<int>(sfx->size()); ++i)
        {
            const auto& entry = (*sfx)[static_cast<size_t>(i)];
            if (!entry.is_object())
                continue;
            if (isEnter ? isConstrainedEnter(entry, neighborId)
                        : isConstrainedExit(entry, neighborId))
            {
                found = i;
                break;
            }
        }

        if (path.empty())
        {
            if (found >= 0)
                sfx->erase(sfx->begin() + found);
            return;
        }

        if (found >= 0)
        {
            (*sfx)[static_cast<size_t>(found)]["path"] = path;
            (*sfx)[static_cast<size_t>(found)]["trigger"] = trigger;
            (*sfx)[static_cast<size_t>(found)][roomKey] = neighborId;
            if (!(*sfx)[static_cast<size_t>(found)].contains("volume"))
                (*sfx)[static_cast<size_t>(found)]["volume"] = 0.85f;
            return;
        }

        nlohmann::json clip = nlohmann::json::object();
        clip["path"] = path;
        clip["trigger"] = trigger;
        clip[roomKey] = neighborId;
        clip["volume"] = 0.85f;
        sfx->push_back(clip);
    };

    upsertOne(true, enterPath);
    upsertOne(false, exitPath);
    docs->markDirty();
    return true;
}


bool SceneGraphModel::exitDirectionAlreadyLeadsTo(
    const std::string& direction,
    const std::string& targetId,
    const std::string& ignoreFromId) const
{
    if (direction.empty() || targetId.empty() || !docs || !docs->scenes.isLoaded())
        return false;

    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (id == ignoreFromId)
            continue;
        if (getExitTarget(id, direction) == targetId)
            return true;
    }
    return false;
}


bool SceneGraphModel::retargetExitLink(
    const std::string& fromId,
    const std::string& direction,
    const std::string& newToId,
    bool maintainReciprocal)
{
    if (!docs || !docs->scenes.isLoaded())
        return false;
    if (fromId.empty() || direction.empty() || newToId.empty())
        return false;
    if (fromId == newToId || !docs->scenes.hasScene(fromId) || !docs->scenes.hasScene(newToId))
        return false;
    if (!isSameLevelLink(fromId, newToId))
        return false;

    const std::string oldToId = getExitTarget(fromId, direction);
    if (oldToId.empty())
        return false;
    if (oldToId == newToId)
        return true;

    // Another scene already uses this direction to reach the chosen target.
    if (exitDirectionAlreadyLeadsTo(direction, newToId, fromId))
        return false;

    const std::string reverseDir = oppositeDirection(direction);
    const bool hadReciprocal =
        maintainReciprocal
        && !reverseDir.empty()
        && getExitTarget(oldToId, reverseDir) == fromId;

    if (hadReciprocal)
    {
        // Reverse slot on the new target must be free (or already back to fromId).
        const std::string existingReverse = getExitTarget(newToId, reverseDir);
        if (!existingReverse.empty() && existingReverse != fromId)
            return false;
    }

    clearExitTarget(fromId, direction);
    if (hadReciprocal)
        clearExitTarget(oldToId, reverseDir);

    setExitTarget(fromId, direction, newToId);
    if (hadReciprocal)
        setExitTarget(newToId, reverseDir, fromId);

    docs->markDirty();
    return true;
}


bool SceneGraphModel::createExitLink(
    const std::string& fromId,
    const std::string& direction,
    const std::string& toId,
    bool reciprocalIfFree)
{
    if (!docs || !docs->scenes.isLoaded())
        return false;
    if (fromId.empty() || direction.empty() || toId.empty())
        return false;
    if (fromId == toId || !docs->scenes.hasScene(fromId) || !docs->scenes.hasScene(toId))
        return false;
    if (!docs->scenes.hasMapPlacement(fromId) || !docs->scenes.hasMapPlacement(toId))
        return false;
    if (!isSameLevelLink(fromId, toId))
        return false;
    if (direction != "forward" && direction != "backward" && direction != "left" && direction != "right")
        return false;

    // Another scene already uses this compass direction to reach the target.
    if (exitDirectionAlreadyLeadsTo(direction, toId, fromId))
        return false;

    const std::string reverseDir = oppositeDirection(direction);
    const std::string existing = getExitTarget(fromId, direction);
    if (!existing.empty() && existing != toId)
    {
        // Replacing an existing link — clear old reciprocal if it pointed back.
        if (!reverseDir.empty() && getExitTarget(existing, reverseDir) == fromId)
            clearExitTarget(existing, reverseDir);
        clearExitTarget(fromId, direction);
    }

    setExitTarget(fromId, direction, toId);

    if (reciprocalIfFree && !reverseDir.empty())
    {
        const std::string reverseExisting = getExitTarget(toId, reverseDir);
        if (reverseExisting.empty() || reverseExisting == fromId)
            setExitTarget(toId, reverseDir, fromId);
    }

    docs->markDirty();
    return true;
}


void SceneGraphModel::recomputeLevelsFromExits()
{
    if (!docs->scenes.isLoaded())
        return;

    const std::vector<std::string> ids = docs->scenes.sceneIds();
    // Prefer vertical (non-zero) level deltas when both exist between a pair
    // (e.g. up to summit must win over a mistaken same-floor back link).
    std::map<std::string, int> directedDelta;

    auto edgeKey = [](const std::string& fromId, const std::string& toId) -> std::string
    {
        return fromId + "\n" + toId;
    };

    auto addEdge = [&](const std::string& fromId, const std::string& toId, int delta)
    {
        if (toId.empty() || !docs->scenes.hasScene(toId))
            return;

        const std::string key = edgeKey(fromId, toId);
        std::map<std::string, int>::iterator existing = directedDelta.find(key);
        if (existing == directedDelta.end())
        {
            directedDelta[key] = delta;
            return;
        }

        // Keep vertical relationships over same-floor links.
        if (existing->second == 0 && delta != 0)
            existing->second = delta;
        else if (existing->second != 0 && delta != 0 && existing->second != delta)
            existing->second = delta; // last non-zero wins; rare conflict
    };

    for (const std::string& id : ids)
    {
        // Vertical links change floor; horizontal links stay on the same floor.
        addEdge(id, getExitTarget(id, "up"), 1);
        addEdge(id, getExitTarget(id, "down"), -1);
        addEdge(id, getExitTarget(id, "forward"), 0);
        addEdge(id, getExitTarget(id, "backward"), 0);
        addEdge(id, getExitTarget(id, "left"), 0);
        addEdge(id, getExitTarget(id, "right"), 0);
    }

    std::map<std::string, std::vector<std::pair<std::string, int> > > edges;
    for (std::map<std::string, int>::const_iterator it = directedDelta.begin();
         it != directedDelta.end();
         ++it)
    {
        const std::string& key = it->first;
        const size_t split = key.find('\n');
        if (split == std::string::npos)
            continue;
        const std::string fromId = key.substr(0, split);
        const std::string toId = key.substr(split + 1);
        const int delta = it->second;
        edges[fromId].push_back(std::make_pair(toId, delta));

        // Bidirectional connectivity for BFS; reverse may already be explicit.
        const std::string reverseKey = edgeKey(toId, fromId);
        if (directedDelta.count(reverseKey) == 0)
            edges[toId].push_back(std::make_pair(fromId, -delta));
    }

    std::map<std::string, int> levels;
    std::queue<std::string> queue;

    auto seed = [&](const std::string& id, int level)
    {
        if (levels.count(id) != 0)
            return;
        levels[id] = level;
        queue.push(id);
    };

    for (const std::string& id : ids)
    {
        const nlohmann::json* scene = docs->scenes.sceneJson(id);
        if (scene != nullptr && scene->value("start", false))
            seed(id, 0);
    }

    if (levels.empty() && !ids.empty())
        seed(ids.front(), 0);

    while (!queue.empty())
    {
        const std::string current = queue.front();
        queue.pop();
        const int currentLevel = levels[current];

        const std::vector<std::pair<std::string, int> >& links = edges[current];
        for (size_t i = 0; i < links.size(); ++i)
        {
            const std::string& nextId = links[i].first;
            const int nextLevel = currentLevel + links[i].second;
            std::map<std::string, int>::iterator existing = levels.find(nextId);
            if (existing == levels.end())
            {
                levels[nextId] = nextLevel;
                queue.push(nextId);
            }
        }
    }

    // Seed remaining connected components (e.g. saloon cluster without start=true).
    for (const std::string& id : ids)
    {
        if (levels.count(id) != 0)
            continue;
        if (edges.count(id) == 0 || edges[id].empty())
            continue;

        levels[id] = 0;
        queue.push(id);
        while (!queue.empty())
        {
            const std::string current = queue.front();
            queue.pop();
            const int currentLevel = levels[current];
            const std::vector<std::pair<std::string, int> >& links = edges[current];
            for (size_t i = 0; i < links.size(); ++i)
            {
                const std::string& nextId = links[i].first;
                if (levels.count(nextId) != 0)
                    continue;
                levels[nextId] = currentLevel + links[i].second;
                queue.push(nextId);
            }
        }
    }

    for (const std::string& id : ids)
    {
        if (levels.count(id) == 0)
            continue;
        // Never invent map placement for list-only (unplaced) scenes.
        if (!docs->scenes.hasMapPlacement(id))
            continue;

        SceneLayout sceneLayout = docs->scenes.getLayout(id);
        sceneLayout.level = levels[id];
        docs->scenes.setLayout(id, sceneLayout);
    }
}


void SceneGraphModel::getLevelRange(int& outMin, int& outMax) const
{
    outMin = 0;
    outMax = 0;
    if (!docs->scenes.isLoaded())
        return;

    bool any = false;
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        const int level = docs->scenes.getLayout(id).level;
        if (!any)
        {
            outMin = level;
            outMax = level;
            any = true;
        }
        else
        {
            if (level < outMin)
                outMin = level;
            if (level > outMax)
                outMax = level;
        }
    }
}


int SceneGraphModel::countScenesOnLevel(int level) const
{
    int count = 0;
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        if (docs->scenes.getLayout(id).level == level)
            ++count;
    }
    return count;
}


std::vector<std::string> SceneGraphModel::scenesOnLevel(int level) const
{
    std::vector<std::string> out;
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        if (docs->scenes.getLayout(id).level == level)
            out.push_back(id);
    }
    return out;
}


bool SceneGraphModel::isSameLevelLink(const std::string& fromId, const std::string& toId) const
{
    if (!docs->scenes.hasScene(fromId) || !docs->scenes.hasScene(toId))
        return false;
    return docs->scenes.getLayout(fromId).level == docs->scenes.getLayout(toId).level;
}


bool SceneGraphModel::directionDelta(const std::string& direction, int& outDCol, int& outDRow) const
{
    if (direction == "right")
    {
        outDCol = 1;
        outDRow = 0;
        return true;
    }
    if (direction == "left")
    {
        outDCol = -1;
        outDRow = 0;
        return true;
    }
    // forward = "into" the room / up the screen; backward = toward the viewer.
    if (direction == "forward")
    {
        outDCol = 0;
        outDRow = -1;
        return true;
    }
    if (direction == "backward")
    {
        outDCol = 0;
        outDRow = 1;
        return true;
    }
    return false;
}


std::string SceneGraphModel::cellKey(int col, int row) const
{
    return std::to_string(col) + "," + std::to_string(row);
}


void SceneGraphModel::cleanupLayoutLevel(int level)
{
    if (!docs || !docs->scenes.isLoaded())
        return;

    const std::vector<std::string> levelIds = scenesOnLevel(level);
    if (levelIds.empty())
        return;

    // Continuous positions — do not re-grid / reshuffle the floor.
    std::map<std::string, Vector2> pos;
    std::map<std::string, float> height;
    for (const std::string& id : levelIds)
    {
        const SceneLayout layout = docs->scenes.getLayout(id);
        pos[id] = {layout.x, layout.y};
        float h = kSceneCardMinHeight;
        if (canvas != nullptr)
            h = canvas->measureSceneCard(id).height;
        height[id] = h;
    }

    const float cardW = kSceneCardWidth;
    auto midX = [&](const std::string& id) {
        return pos[id].x + cardW * 0.5f;
    };
    auto midY = [&](const std::string& id) {
        return pos[id].y + height[id] * 0.5f;
    };

    struct Link
    {
        std::string from;
        std::string to;
        std::string dir;
        int dCol = 0;
        int dRow = 0;
    };
    std::vector<Link> links;
    const char* dirs[] = {"forward", "backward", "left", "right"};
    for (const std::string& id : levelIds)
    {
        for (const char* dir : dirs)
        {
            const std::string target = getExitTarget(id, dir);
            if (target.empty() || pos.count(target) == 0)
                continue;
            if (!isSameLevelLink(id, target))
                continue;
            Link link;
            link.from = id;
            link.to = target;
            link.dir = dir;
            if (!directionDelta(dir, link.dCol, link.dRow))
                continue;
            links.push_back(link);
        }
    }

    // Align ports on linked pairs so orthogonal stubs share an axis (straight mid-run).
    // L/R → match mid-Y; F/B → match mid-X. Preserve neighborhood; average both cards.
    for (int iter = 0; iter < 8; ++iter)
    {
        bool changed = false;
        for (const Link& link : links)
        {
            if (link.dCol != 0)
            {
                // Horizontal exit: align vertical centers.
                const float a = midY(link.from);
                const float b = midY(link.to);
                const float avg = 0.5f * (a + b);
                const float newFromY = avg - height[link.from] * 0.5f;
                const float newToY = avg - height[link.to] * 0.5f;
                if (std::fabs(newFromY - pos[link.from].y) > 0.25f
                    || std::fabs(newToY - pos[link.to].y) > 0.25f)
                {
                    pos[link.from].y = newFromY;
                    pos[link.to].y = newToY;
                    changed = true;
                }
            }
            else if (link.dRow != 0)
            {
                // Vertical exit: align horizontal centers (width is uniform).
                const float a = midX(link.from);
                const float b = midX(link.to);
                const float avg = 0.5f * (a + b);
                const float newFromX = avg - cardW * 0.5f;
                const float newToX = avg - cardW * 0.5f;
                if (std::fabs(newFromX - pos[link.from].x) > 0.25f
                    || std::fabs(newToX - pos[link.to].x) > 0.25f)
                {
                    pos[link.from].x = newFromX;
                    pos[link.to].x = newToX;
                    changed = true;
                }
            }
        }
        if (!changed)
            break;
    }

    // Minimal overlap separation so cards do not stack after alignment.
    const float gapX = kLayoutGapX * 0.35f;
    const float gapY = kLayoutGapY * 0.35f;
    for (int iter = 0; iter < 16; ++iter)
    {
        bool moved = false;
        for (size_t i = 0; i < levelIds.size(); ++i)
        {
            for (size_t j = i + 1; j < levelIds.size(); ++j)
            {
                const std::string& a = levelIds[i];
                const std::string& b = levelIds[j];
                const float ax0 = pos[a].x;
                const float ay0 = pos[a].y;
                const float ax1 = ax0 + cardW;
                const float ay1 = ay0 + height[a];
                const float bx0 = pos[b].x;
                const float by0 = pos[b].y;
                const float bx1 = bx0 + cardW;
                const float by1 = by0 + height[b];

                const float overlapX = std::min(ax1, bx1) - std::max(ax0, bx0);
                const float overlapY = std::min(ay1, by1) - std::max(ay0, by0);
                if (overlapX <= 0.0f || overlapY <= 0.0f)
                    continue;

                // Push apart along the shallower penetration, plus a small gap.
                if (overlapX < overlapY)
                {
                    const float push = (overlapX + gapX) * 0.5f;
                    if (ax0 + cardW * 0.5f <= bx0 + cardW * 0.5f)
                    {
                        pos[a].x -= push;
                        pos[b].x += push;
                    }
                    else
                    {
                        pos[a].x += push;
                        pos[b].x -= push;
                    }
                }
                else
                {
                    const float push = (overlapY + gapY) * 0.5f;
                    if (ay0 + height[a] * 0.5f <= by0 + height[b] * 0.5f)
                    {
                        pos[a].y -= push;
                        pos[b].y += push;
                    }
                    else
                    {
                        pos[a].y += push;
                        pos[b].y -= push;
                    }
                }
                moved = true;
            }
        }
        if (!moved)
            break;
    }

    // Re-run a couple of port-align passes after separation (separation can skew mids).
    for (int iter = 0; iter < 4; ++iter)
    {
        for (const Link& link : links)
        {
            if (link.dCol != 0)
            {
                const float avg = 0.5f * (midY(link.from) + midY(link.to));
                pos[link.from].y = avg - height[link.from] * 0.5f;
                pos[link.to].y = avg - height[link.to] * 0.5f;
            }
            else if (link.dRow != 0)
            {
                const float avg = 0.5f * (midX(link.from) + midX(link.to));
                pos[link.from].x = avg - cardW * 0.5f;
                pos[link.to].x = avg - cardW * 0.5f;
            }
        }
    }

    for (const std::string& id : levelIds)
    {
        SceneLayout layout = docs->scenes.getLayout(id);
        layout.x = pos[id].x;
        layout.y = pos[id].y;
        layout.level = level;
        docs->scenes.setLayout(id, layout);
    }

    docs->markDirty();
}


void SceneGraphModel::autoLayoutLevel(int level)
{
    const std::vector<std::string> levelIds = scenesOnLevel(level);
    if (levelIds.empty())
        return;

    // Directed same-level exits: id -> list of (target, direction).
    std::map<std::string, std::vector<std::pair<std::string, std::string> > > neighbors;
    // Undirected adjacency for shortest-path distances on this floor.
    std::map<std::string, std::vector<std::string> > undirected;
    for (const std::string& id : levelIds)
    {
        const char* dirs[] = {"forward", "backward", "left", "right"};
        for (size_t i = 0; i < 4; ++i)
        {
            const std::string target = getExitTarget(id, dirs[i]);
            if (target.empty() || !isSameLevelLink(id, target))
                continue;
            neighbors[id].push_back(std::make_pair(target, std::string(dirs[i])));
            undirected[id].push_back(target);
            undirected[target].push_back(id);
        }
    }
    for (std::map<std::string, std::vector<std::string> >::iterator it = undirected.begin();
         it != undirected.end();
         ++it)
    {
        std::vector<std::string>& list = it->second;
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    // BFS hop distances from a root within one connected component.
    auto bfsDistances = [&](const std::string& root) -> std::map<std::string, int>
    {
        std::map<std::string, int> dist;
        std::queue<std::string> q;
        dist[root] = 0;
        q.push(root);
        while (!q.empty())
        {
            const std::string cur = q.front();
            q.pop();
            const std::vector<std::string>& links = undirected[cur];
            for (size_t i = 0; i < links.size(); ++i)
            {
                const std::string& nxt = links[i];
                if (dist.count(nxt) != 0)
                    continue;
                dist[nxt] = dist[cur] + 1;
                q.push(nxt);
            }
        }
        return dist;
    };

    // Graph center: minimize sum of shortest-path distances (then eccentricity).
    auto pickComponentRoot = [&](const std::vector<std::string>& component) -> std::string
    {
        std::string bestId = component.front();
        long long bestSum = -1;
        int bestEcc = -1;
        for (size_t i = 0; i < component.size(); ++i)
        {
            const nlohmann::json* scene = docs->scenes.sceneJson(component[i]);
            if (scene != nullptr && scene->value("start", false))
                return component[i];
        }
        for (size_t i = 0; i < component.size(); ++i)
        {
            const std::map<std::string, int> dist = bfsDistances(component[i]);
            long long sum = 0;
            int ecc = 0;
            int reached = 0;
            for (size_t j = 0; j < component.size(); ++j)
            {
                std::map<std::string, int>::const_iterator it = dist.find(component[j]);
                if (it == dist.end())
                    continue;
                sum += it->second;
                ecc = std::max(ecc, it->second);
                ++reached;
            }
            if (reached == 0)
                continue;
            if (bestSum < 0 || sum < bestSum || (sum == bestSum && ecc < bestEcc))
            {
                bestSum = sum;
                bestEcc = ecc;
                bestId = component[i];
            }
        }
        return bestId;
    };

    std::map<std::string, std::pair<int, int> > grid; // id -> (col,row)
    std::set<std::string> occupiedCells;
    std::set<std::string> placed;

    auto placeAt = [&](const std::string& id, int col, int row)
    {
        // Relocate if already placed (used by refinement).
        if (placed.count(id) != 0)
        {
            const std::pair<int, int> old = grid[id];
            occupiedCells.erase(cellKey(old.first, old.second));
        }
        grid[id] = std::make_pair(col, row);
        occupiedCells.insert(cellKey(col, row));
        placed.insert(id);
    };

    auto isFree = [&](int col, int row) -> bool
    {
        return occupiedCells.count(cellKey(col, row)) == 0;
    };

    // Score a candidate cell: prefer direction-ideal cell and short wires to
    // already-placed neighbors (Manhattan grid distance ≈ path length on map).
    auto scoreCell = [&](
        const std::string& id,
        int col,
        int row,
        int preferredCol,
        int preferredRow) -> int
    {
        int score = 0;
        // Soft pull toward the direction-preferred neighbor cell.
        score += 4 * (std::abs(col - preferredCol) + std::abs(row - preferredRow));

        // Outgoing exits from this scene to already-placed targets.
        const std::vector<std::pair<std::string, std::string> >& outLinks = neighbors[id];
        for (size_t i = 0; i < outLinks.size(); ++i)
        {
            if (placed.count(outLinks[i].first) == 0)
                continue;
            const std::pair<int, int> other = grid[outLinks[i].first];
            const int dist = std::abs(col - other.first) + std::abs(row - other.second);
            // Ideal same-level exit is one cell away.
            score += 12 * dist;
            int dCol = 0;
            int dRow = 0;
            if (directionDelta(outLinks[i].second, dCol, dRow))
            {
                const int idealCol = col + dCol;
                const int idealRow = row + dRow;
                score += 8 * (std::abs(idealCol - other.first) + std::abs(idealRow - other.second));
            }
        }

        // Incoming exits from already-placed scenes.
        for (size_t i = 0; i < levelIds.size(); ++i)
        {
            const std::string& otherId = levelIds[i];
            if (placed.count(otherId) == 0 || otherId == id)
                continue;
            const std::vector<std::pair<std::string, std::string> >& fromOther = neighbors[otherId];
            for (size_t j = 0; j < fromOther.size(); ++j)
            {
                if (fromOther[j].first != id)
                    continue;
                const std::pair<int, int> other = grid[otherId];
                const int dist = std::abs(col - other.first) + std::abs(row - other.second);
                score += 12 * dist;
                int dCol = 0;
                int dRow = 0;
                if (directionDelta(fromOther[j].second, dCol, dRow))
                {
                    const int idealCol = other.first + dCol;
                    const int idealRow = other.second + dRow;
                    score += 8 * (std::abs(idealCol - col) + std::abs(idealRow - row));
                }
            }
        }
        return score;
    };

    auto findBestCell = [&](
        const std::string& id,
        int preferredCol,
        int preferredRow,
        int& outCol,
        int& outRow) -> bool
    {
        bool found = false;
        int bestScore = 0;
        int bestCol = preferredCol;
        int bestRow = preferredRow;

        for (int radius = 0; radius <= 32; ++radius)
        {
            for (int dCol = -radius; dCol <= radius; ++dCol)
            {
                for (int dRow = -radius; dRow <= radius; ++dRow)
                {
                    if (radius > 0 && std::abs(dCol) != radius && std::abs(dRow) != radius)
                        continue;
                    const int col = preferredCol + dCol;
                    const int row = preferredRow + dRow;
                    if (!isFree(col, row))
                        continue;
                    const int score = scoreCell(id, col, row, preferredCol, preferredRow);
                    if (!found || score < bestScore)
                    {
                        found = true;
                        bestScore = score;
                        bestCol = col;
                        bestRow = row;
                    }
                }
            }
            // First radius ring with any free cell: take best in that ring (and
            // radius 0 if free). Searching further only if nothing free yet.
            if (found && radius >= 0)
            {
                // Continue a couple of rings so a free cell one step farther
                // can win if it keeps wires much shorter.
                if (radius >= 2)
                    break;
            }
        }

        if (!found)
            return false;
        outCol = bestCol;
        outRow = bestRow;
        return true;
    };

    // Discover components via undirected BFS; layout each separately.
    std::set<std::string> seenComponent;
    std::vector<std::vector<std::string> > components;
    for (size_t i = 0; i < levelIds.size(); ++i)
    {
        const std::string& seed = levelIds[i];
        if (seenComponent.count(seed) != 0)
            continue;
        std::vector<std::string> component;
        std::queue<std::string> q;
        q.push(seed);
        seenComponent.insert(seed);
        while (!q.empty())
        {
            const std::string cur = q.front();
            q.pop();
            component.push_back(cur);
            const std::vector<std::string>& links = undirected[cur];
            for (size_t j = 0; j < links.size(); ++j)
            {
                if (seenComponent.count(links[j]) != 0)
                    continue;
                seenComponent.insert(links[j]);
                q.push(links[j]);
            }
        }
        // Isolated nodes (no undirected links) still form a component of one.
        if (component.empty())
            component.push_back(seed);
        components.push_back(component);
    }

    // Stable component order: start-containing first, then larger graphs.
    std::sort(components.begin(), components.end(),
        [&](const std::vector<std::string>& a, const std::vector<std::string>& b)
        {
            auto hasStart = [&](const std::vector<std::string>& c) -> bool
            {
                for (size_t i = 0; i < c.size(); ++i)
                {
                    const nlohmann::json* scene = docs->scenes.sceneJson(c[i]);
                    if (scene != nullptr && scene->value("start", false))
                        return true;
                }
                return false;
            };
            const bool sa = hasStart(a);
            const bool sb = hasStart(b);
            if (sa != sb)
                return sa;
            return a.size() > b.size();
        });

    int componentOffsetCol = 0;

    for (size_t c = 0; c < components.size(); ++c)
    {
        const std::vector<std::string>& component = components[c];
        if (component.empty())
            continue;

        const std::string rootId = pickComponentRoot(component);
        int seedCol = 0;
        int seedRow = 0;
        if (!findBestCell(rootId, componentOffsetCol, 0, seedCol, seedRow))
        {
            seedCol = componentOffsetCol;
            seedRow = 0;
            // Force place even if occupied (should not happen for fresh offset).
            while (!isFree(seedCol, seedRow))
                ++seedCol;
        }
        placeAt(rootId, seedCol, seedRow);

        // BFS from root so shorter graph paths place earlier (near the center).
        std::queue<std::string> queue;
        queue.push(rootId);
        while (!queue.empty())
        {
            const std::string current = queue.front();
            queue.pop();
            const std::pair<int, int> currentCell = grid[current];

            // Order links by direction for stability, not by string id alone.
            std::vector<std::pair<std::string, std::string> > links = neighbors[current];
            std::sort(links.begin(), links.end(),
                [](const std::pair<std::string, std::string>& a,
                   const std::pair<std::string, std::string>& b)
                {
                    static const char* order[] = {
                        "forward", "right", "backward", "left"};
                    auto rank = [&](const std::string& d) -> int
                    {
                        for (int i = 0; i < 4; ++i)
                        {
                            if (d == order[i])
                                return i;
                        }
                        return 99;
                    };
                    const int ra = rank(a.second);
                    const int rb = rank(b.second);
                    if (ra != rb)
                        return ra < rb;
                    return a.first < b.first;
                });

            for (size_t i = 0; i < links.size(); ++i)
            {
                const std::string& nextId = links[i].first;
                if (placed.count(nextId) != 0)
                    continue;
                // Only place nodes in this component.
                if (std::find(component.begin(), component.end(), nextId) == component.end())
                    continue;

                int dCol = 0;
                int dRow = 0;
                int preferredCol = currentCell.first;
                int preferredRow = currentCell.second;
                if (directionDelta(links[i].second, dCol, dRow))
                {
                    preferredCol += dCol;
                    preferredRow += dRow;
                }
                else
                {
                    preferredCol += 1;
                }

                int freeCol = preferredCol;
                int freeRow = preferredRow;
                if (!findBestCell(nextId, preferredCol, preferredRow, freeCol, freeRow))
                    continue;

                placeAt(nextId, freeCol, freeRow);
                queue.push(nextId);
            }
        }

        // Place remaining component members (unreachable via directed exits only).
        for (size_t i = 0; i < component.size(); ++i)
        {
            if (placed.count(component[i]) != 0)
                continue;
            int freeCol = 0;
            int freeRow = 0;
            if (!findBestCell(component[i], componentOffsetCol, 0, freeCol, freeRow))
            {
                freeCol = componentOffsetCol;
                freeRow = 0;
                while (!isFree(freeCol, freeRow))
                    ++freeCol;
            }
            placeAt(component[i], freeCol, freeRow);
        }

        int maxCol = componentOffsetCol;
        for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
             it != grid.end();
             ++it)
        {
            if (it->second.first > maxCol)
                maxCol = it->second.first;
        }
        componentOffsetCol = maxCol + 3;
    }

    // Local search: slide scenes on free cells to minimize total wire length
    // (Manhattan grid distance over directed same-level exits).
    auto edgeCostFor = [&](const std::string& id, int col, int row) -> int
    {
        int cost = 0;
        const std::vector<std::pair<std::string, std::string> >& outLinks = neighbors[id];
        for (size_t i = 0; i < outLinks.size(); ++i)
        {
            if (placed.count(outLinks[i].first) == 0)
                continue;
            const std::pair<int, int> other =
                (outLinks[i].first == id) ? std::make_pair(col, row) : grid[outLinks[i].first];
            // If target is this id (shouldn't), skip.
            if (outLinks[i].first == id)
                continue;
            const int dist = std::abs(col - other.first) + std::abs(row - other.second);
            cost += dist * dist; // quadratic: strongly prefer unit steps
            int dCol = 0;
            int dRow = 0;
            if (directionDelta(outLinks[i].second, dCol, dRow))
            {
                const int idealCol = col + dCol;
                const int idealRow = row + dRow;
                cost += 2 * (std::abs(idealCol - other.first) + std::abs(idealRow - other.second));
            }
        }
        for (size_t i = 0; i < levelIds.size(); ++i)
        {
            const std::string& otherId = levelIds[i];
            if (otherId == id || placed.count(otherId) == 0)
                continue;
            const std::vector<std::pair<std::string, std::string> >& fromOther = neighbors[otherId];
            for (size_t j = 0; j < fromOther.size(); ++j)
            {
                if (fromOther[j].first != id)
                    continue;
                const std::pair<int, int> other = grid[otherId];
                const int dist = std::abs(col - other.first) + std::abs(row - other.second);
                cost += dist * dist;
                int dCol = 0;
                int dRow = 0;
                if (directionDelta(fromOther[j].second, dCol, dRow))
                {
                    const int idealCol = other.first + dCol;
                    const int idealRow = other.second + dRow;
                    cost += 2 * (std::abs(idealCol - col) + std::abs(idealRow - row));
                }
            }
        }
        return cost;
    };

    for (int iter = 0; iter < 24; ++iter)
    {
        bool improved = false;
        for (size_t i = 0; i < levelIds.size(); ++i)
        {
            const std::string& id = levelIds[i];
            if (placed.count(id) == 0)
                continue;
            const std::pair<int, int> cur = grid[id];
            const int curCost = edgeCostFor(id, cur.first, cur.second);
            int bestCol = cur.first;
            int bestRow = cur.second;
            int bestCost = curCost;

            for (int dCol = -3; dCol <= 3; ++dCol)
            {
                for (int dRow = -3; dRow <= 3; ++dRow)
                {
                    if (dCol == 0 && dRow == 0)
                        continue;
                    const int col = cur.first + dCol;
                    const int row = cur.second + dRow;
                    if (!isFree(col, row))
                        continue;
                    const int cost = edgeCostFor(id, col, row);
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        bestCol = col;
                        bestRow = row;
                    }
                }
            }

            if (bestCol != cur.first || bestRow != cur.second)
            {
                placeAt(id, bestCol, bestRow);
                improved = true;
            }
        }
        if (!improved)
            break;
    }

    int minCol = 0;
    int minRow = 0;
    bool any = false;
    for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
         it != grid.end();
         ++it)
    {
        if (!any)
        {
            minCol = it->second.first;
            minRow = it->second.second;
            any = true;
        }
        else
        {
            minCol = std::min(minCol, it->second.first);
            minRow = std::min(minRow, it->second.second);
        }
    }

    float cellHeight = kSceneCardMinHeight;
    for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
         it != grid.end();
         ++it)
    {
        if (canvas == nullptr)
            break;
        const float h = canvas->measureSceneCard(it->first).height;
        if (h > cellHeight)
            cellHeight = h;
    }

    const float pitchX = kSceneCardWidth + kLayoutGapX;
    const float pitchY = cellHeight + kLayoutGapY;
    for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
         it != grid.end();
         ++it)
    {
        SceneLayout sceneLayout = docs->scenes.getLayout(it->first);
        // Card width is uniform; vertically center shorter cards in the row cell
        // so left/right mid-edge ports share a Y and wires can run straight.
        float cardH = kSceneCardMinHeight;
        if (canvas != nullptr)
            cardH = canvas->measureSceneCard(it->first).height;
        const float yPad = std::max(0.0f, (cellHeight - cardH) * 0.5f);
        sceneLayout.x = kLayoutOriginX + static_cast<float>(it->second.first - minCol) * pitchX;
        sceneLayout.y = kLayoutOriginY
            + static_cast<float>(it->second.second - minRow) * pitchY
            + yPad;
        sceneLayout.level = level;
        docs->scenes.setLayout(it->first, sceneLayout);
    }

    docs->markDirty();
}


void SceneGraphModel::autoLayoutAllLevels()
{
    int minLevel = 0;
    int maxLevel = 0;
    getLevelRange(minLevel, maxLevel);
    for (int level = minLevel; level <= maxLevel; ++level)
        autoLayoutLevel(level);
}


void SceneGraphModel::ensureDefaultLayouts()
{
    if (!docs->scenes.isLoaded())
        return;

    recomputeLevelsFromExits();
    autoLayoutAllLevels();

    int minLevel = 0;
    int maxLevel = 0;
    getLevelRange(minLevel, maxLevel);
    if ((*canvasLevel) < minLevel || (*canvasLevel) > maxLevel)
        (*canvasLevel) = 0;
    if ((*canvasLevel) < minLevel)
        (*canvasLevel) = minLevel;
    if ((*canvasLevel) > maxLevel)
        (*canvasLevel) = maxLevel;
}


void SceneGraphModel::applyStackLink(bool placeAbove)
{
    if (!docs->scenes.hasScene(stackSourceId) || !docs->scenes.hasScene(stackTargetId))
        return;

    if (placeAbove)
    {
        setExitTarget(stackTargetId, "up", stackSourceId);
        setExitTarget(stackSourceId, "down", stackTargetId);
    }
    else
    {
        setExitTarget(stackTargetId, "down", stackSourceId);
        setExitTarget(stackSourceId, "up", stackTargetId);
    }

    recomputeLevelsFromExits();
    autoLayoutAllLevels();
    (*canvasLevel) = docs->scenes.getLayout(stackSourceId).level;
    (*selectionSceneId) = stackSourceId;
    docs->markDirty();
}


void SceneGraphModel::closeStackDialog()
{
    stackDialogOpen = false;
    stackSourceId.clear();
    stackTargetId.clear();
}


std::string SceneGraphModel::findStackTarget(const Rectangle& ghost, Rectangle canvasBounds, const std::string& excludeId) const
{
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (id == excludeId)
            continue;
        if (docs->scenes.getLayout(id).level != (*canvasLevel))
            continue;

        const Rectangle card = canvas->sceneCardBounds(id, canvasBounds);
        if (CheckCollisionRecs(ghost, card))
            return id;
    }
    return "";
}


std::string SceneGraphModel::oppositeDirection(const std::string& direction) const
{
    if (direction == "left")
        return "right";
    if (direction == "right")
        return "left";
    if (direction == "forward")
        return "backward";
    if (direction == "backward")
        return "forward";
    return "";
}
} // namespace timberline_editor
