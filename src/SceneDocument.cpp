#include "SceneDocument.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace testgame
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

bool SceneDocument::hasScene(const std::string& sceneId) const
{
    return isLoaded() && root["scenes"].contains(sceneId);
}

SceneLayout SceneDocument::getLayout(const std::string& sceneId) const
{
    SceneLayout layout;
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr || !scene->contains("layout") || !(*scene)["layout"].is_object())
        return layout;

    const nlohmann::json& layoutJson = (*scene)["layout"];
    layout.x = layoutJson.value("x", 0.0f);
    layout.y = layoutJson.value("y", 0.0f);
    layout.level = layoutJson.value("level", 0);
    return layout;
}

void SceneDocument::setLayout(const std::string& sceneId, const SceneLayout& layout)
{
    nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr)
        return;

    (*scene)["layout"] = {
        {"x", layout.x},
        {"y", layout.y},
        {"level", layout.level}
    };
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

void SceneDocument::setActors(const std::string& sceneId, const std::vector<SceneActor>& actors)
{
    nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr)
        return;

    nlohmann::json actorsJson = nlohmann::json::array();
    for (const SceneActor& actor : actors)
    {
        actorsJson.push_back({
            {"id", actor.id},
            {"name", actor.name},
            {"role", actor.role},
            {"x", actor.x},
            {"y", actor.y}
        });
    }

    (*scene)["actors"] = actorsJson;
}

std::string SceneDocument::getSceneImagePath(const std::string& sceneId) const
{
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr)
        return "";
    return scene->value("image", "");
}

std::string SceneDocument::getSceneDescription(const std::string& sceneId) const
{
    const nlohmann::json* scene = sceneJson(sceneId);
    if (scene == nullptr)
        return "";
    return scene->value("description", "");
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