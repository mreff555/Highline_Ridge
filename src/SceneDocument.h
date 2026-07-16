#ifndef SCENE_DOCUMENT_H
#define SCENE_DOCUMENT_H

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace testgame
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

    SceneLayout getLayout(const std::string& sceneId) const;
    void setLayout(const std::string& sceneId, const SceneLayout& layout);

    std::vector<SceneActor> getActors(const std::string& sceneId) const;
    void setActors(const std::string& sceneId, const std::vector<SceneActor>& actors);

    std::string getSceneImagePath(const std::string& sceneId) const;
    std::string getSceneDescription(const std::string& sceneId) const;

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