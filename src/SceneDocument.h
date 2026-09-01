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

#ifndef SCENE_DOCUMENT_H
#define SCENE_DOCUMENT_H

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace timberline_engine
{

struct SceneLayout
{
    float x = 0.0f;
    float y = 0.0f;
    int level = 0;
};

struct SceneActor
{
    std::string id;
    std::string name;
    std::string role;
    float x = 0.0f;
    float y = 0.0f;
};

class SceneDocument
{
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool save() const;

    const std::string& path() const { return filePath; }
    void setPath(const std::string& path) { filePath = path; }

    bool isLoaded() const { return root.is_object() && root.contains("scenes"); }

    std::vector<std::string> sceneIds() const;
    bool hasScene(const std::string& sceneId) const;

    /** True when the scene JSON has a layout object (shown on the map). */
    bool hasMapPlacement(const std::string& sceneId) const;

    // Remove a scene and clear inbound exits/movement/exitRequirements on others.
    bool removeScene(const std::string& sceneId);

    /**
     * Rename scenes[oldId] → scenes[newId] and retarget inbound exit strings.
     * Fails if old missing, new empty/taken, or ids equal. Does not write disk.
     */
    bool renameScene(const std::string& oldId, const std::string& newId);

    /**
     * Insert a new scene object under scenes[sceneId]. Fails if the id already
     * exists or the document is not loaded. Does not write disk.
     */
    bool createScene(const std::string& sceneId, const nlohmann::json& sceneObject);

    /**
     * Deep-copy sourceId to a new unique id (source, source_2, …).
     * Clears layout and outbound exits/movement/exitRequirements so the clone
     * is an independent room. Returns the new id, or empty on failure.
     */
    std::string duplicateScene(const std::string& sourceId);

    /** Next free id: base, base_2, base_3, … */
    std::string allocateUniqueSceneId(const std::string& baseId) const;

    SceneLayout getLayout(const std::string& sceneId) const;
    void setLayout(const std::string& sceneId, const SceneLayout& layout);
    void clearLayout(const std::string& sceneId);

    std::vector<SceneActor> getActors(const std::string& sceneId) const;

    std::string getSceneImagePath(const std::string& sceneId) const;

    /** First music bed path under audio.music, or empty. */
    std::string getSceneMusicPath(const std::string& sceneId) const;

    /** First ambient bed path under audio.ambient[], or empty. */
    std::string getSceneAmbientPath(const std::string& sceneId) const;

    const nlohmann::json& document() const { return root; }
    nlohmann::json* sceneJson(const std::string& sceneId);
    const nlohmann::json* sceneJson(const std::string& sceneId) const;

    std::vector<std::pair<std::string, std::string>> sceneVariableRows(
        const std::string& sceneId) const;

private:
    nlohmann::json root = nlohmann::json::object();
    std::string filePath;
};

}

#endif /* SCENE_DOCUMENT_H */