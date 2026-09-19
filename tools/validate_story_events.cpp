/*******************************************************************************
 * Timberline engine — storyEvents + story-flag graph lint (#33 Phase P3)
 * Copyright (C) 2026 Dan Feerst
 *
 * Hard-fail: invalid storyEvents schema / duplicate ids.
 * Soft-warn: flag set-never-read, require-never-set, C++ inserts not in JSON.
 ******************************************************************************/

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{

bool loadJsonFile(const std::string& path, nlohmann::json& out)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "ERROR: cannot open " << path << "\n";
        return false;
    }
    try
    {
        in >> out;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ERROR: parse failed for " << path << ": " << ex.what() << "\n";
        return false;
    }
    return true;
}

void addString(std::set<std::string>& dest, const nlohmann::json& node, const char* key)
{
    if (!node.is_object() || !node.contains(key) || !node[key].is_string())
        return;
    const std::string value = node[key].get<std::string>();
    if (!value.empty())
        dest.insert(value);
}

void addStringArray(std::set<std::string>& dest, const nlohmann::json& node, const char* key)
{
    if (!node.is_object() || !node.contains(key) || !node[key].is_array())
        return;
    for (const nlohmann::json& entry : node[key])
    {
        if (entry.is_string() && !entry.get<std::string>().empty())
            dest.insert(entry.get<std::string>());
    }
}

void collectJsonStringFields(
    const nlohmann::json& node,
    const char* key,
    std::set<std::string>& dest)
{
    if (node.is_object())
    {
        addString(dest, node, key);
        for (auto it = node.begin(); it != node.end(); ++it)
            collectJsonStringFields(it.value(), key, dest);
    }
    else if (node.is_array())
    {
        for (const nlohmann::json& child : node)
            collectJsonStringFields(child, key, dest);
    }
}

std::string readFileText(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char* argv[])
{
    std::string scenesPath = "resources/scenes.json";
    std::string conversationsPath = "resources/conversations.json";
    std::string gameSessionPath = "src/GameSession.cpp";
    bool warningsAreErrors = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--werror")
        {
            warningsAreErrors = true;
            continue;
        }
        if (arg == "--scenes" && i + 1 < argc)
        {
            scenesPath = argv[++i];
            continue;
        }
        if (arg == "--conversations" && i + 1 < argc)
        {
            conversationsPath = argv[++i];
            continue;
        }
        if (arg == "--game-session" && i + 1 < argc)
        {
            gameSessionPath = argv[++i];
            continue;
        }
        if (arg == "--help" || arg == "-h")
        {
            std::cout
                << "Usage: validate_story_events [--scenes PATH] [--conversations PATH]\n"
                << "       [--game-session PATH] [--werror]\n";
            return 0;
        }
        // Positional fallback: scenes conversations [gameSession]
        if (i == 1)
            scenesPath = arg;
        else if (i == 2)
            conversationsPath = arg;
        else if (i == 3)
            gameSessionPath = arg;
    }

    nlohmann::json scenesDoc;
    nlohmann::json conversationsDoc;
    if (!loadJsonFile(scenesPath, scenesDoc))
        return 1;
    if (!loadJsonFile(conversationsPath, conversationsDoc))
        return 1;

    const nlohmann::json* scenesRoot = &scenesDoc;
    if (scenesDoc.is_object() && scenesDoc.contains("scenes") && scenesDoc["scenes"].is_object())
        scenesRoot = &scenesDoc["scenes"];
    if (!scenesRoot->is_object())
    {
        std::cerr << "ERROR: scenes root is not an object\n";
        return 1;
    }

    int errors = 0;
    int warnings = 0;
    std::set<std::string> setFlags;
    std::set<std::string> readFlags;
    std::set<std::string> globalEventIds;

    auto noteError = [&](const std::string& msg) {
        std::cerr << "ERROR: " << msg << "\n";
        ++errors;
    };
    auto noteWarn = [&](const std::string& msg) {
        std::cerr << "WARN: " << msg << "\n";
        ++warnings;
    };

    for (auto sceneIt = scenesRoot->begin(); sceneIt != scenesRoot->end(); ++sceneIt)
    {
        if (!sceneIt.value().is_object())
            continue;
        const std::string sceneId = sceneIt.key();
        const nlohmann::json& scene = sceneIt.value();

        // Reads / writes from common scene fields.
        addString(setFlags, scene, "examineFlag");
        addString(readFlags, scene, "alternateImageFlag");
        if (scene.contains("alternateImages") && scene["alternateImages"].is_array())
        {
            for (const nlohmann::json& alt : scene["alternateImages"])
                addString(readFlags, alt, "flag");
        }
        if (scene.contains("exitRequirements") && scene["exitRequirements"].is_object())
        {
            for (auto exitIt = scene["exitRequirements"].begin();
                 exitIt != scene["exitRequirements"].end();
                 ++exitIt)
            {
                addString(readFlags, exitIt.value(), "requiresStoryFlag");
            }
        }
        else if (scene.contains("exit_requirements") && scene["exit_requirements"].is_object())
        {
            for (auto exitIt = scene["exit_requirements"].begin();
                 exitIt != scene["exit_requirements"].end();
                 ++exitIt)
            {
                addString(readFlags, exitIt.value(), "requiresStoryFlag");
            }
        }

        if (scene.contains("takeables") && scene["takeables"].is_array())
        {
            for (const nlohmann::json& takeable : scene["takeables"])
                addString(readFlags, takeable, "requiresStoryFlag");
        }

        if (scene.contains("interactions") && scene["interactions"].is_array())
        {
            for (const nlohmann::json& interaction : scene["interactions"])
            {
                addString(setFlags, interaction, "useFlag");
                addString(readFlags, interaction, "hideWhenStoryFlag");
                addString(readFlags, interaction, "ttsVariantFlag");
            }
        }

        if (!scene.contains("storyEvents"))
            continue;
        if (!scene["storyEvents"].is_array())
        {
            noteError(sceneId + ": storyEvents must be an array");
            continue;
        }

        std::set<std::string> localIds;
        for (size_t i = 0; i < scene["storyEvents"].size(); ++i)
        {
            const nlohmann::json& event = scene["storyEvents"][i];
            const std::string where =
                sceneId + ".storyEvents[" + std::to_string(i) + "]";
            if (!event.is_object())
            {
                noteError(where + " is not an object");
                continue;
            }

            const std::string id = event.value("id", "");
            if (id.empty())
            {
                noteError(where + " missing id");
                continue;
            }
            if (!localIds.insert(id).second)
                noteError(sceneId + ": duplicate storyEvent id '" + id + "'");
            if (!globalEventIds.insert(sceneId + "/" + id).second)
                noteError("duplicate storyEvent id path '" + sceneId + "/" + id + "'");

            const std::string when = event.value("when", "");
            if (when != "enter" && when != "exit" && when != "examine")
            {
                noteError(
                    where + " (" + id + "): when must be enter|exit|examine, got '" + when
                    + "'");
            }
            if (when == "exit" && event.value("direction", "").empty())
                noteError(where + " (" + id + "): exit events require direction");

            addStringArray(readFlags, event, "requiresFlags");
            addStringArray(readFlags, event, "unlessFlags");
            addStringArray(setFlags, event, "setsFlags");
            addStringArray(setFlags, event, "clearsFlags");
        }
    }

    // Conversation grant / require flags (recursive scan).
    collectJsonStringFields(conversationsDoc, "requiresFlag", readFlags);
    collectJsonStringFields(conversationsDoc, "grantStoryFlag", setFlags);
    collectJsonStringFields(conversationsDoc, "grantFlag", setFlags);

    // Advisory: C++ storyFlags.insert / .count / .erase string literals.
    if (std::filesystem::exists(gameSessionPath))
    {
        const std::string cpp = readFileText(gameSessionPath);
        static const std::regex insertRe(
            R"(storyFlags\.insert\(\s*\"([^\"]+)\"\s*\))");
        static const std::regex countRe(
            R"(storyFlags\.count\(\s*\"([^\"]+)\"\s*\))");
        static const std::regex eraseRe(
            R"(storyFlags\.erase\(\s*\"([^\"]+)\"\s*\))");

        auto collect = [&](const std::regex& re, std::set<std::string>& dest) {
            std::sregex_iterator it(cpp.begin(), cpp.end(), re);
            std::sregex_iterator end;
            for (; it != end; ++it)
                dest.insert((*it)[1].str());
        };

        std::set<std::string> cppInserts;
        std::set<std::string> cppReads;
        collect(insertRe, cppInserts);
        collect(countRe, cppReads);
        collect(eraseRe, cppInserts); // erase is also a write/mutation

        for (const std::string& flag : cppReads)
            readFlags.insert(flag);

        for (const std::string& flag : cppInserts)
        {
            if (setFlags.count(flag) == 0)
            {
                noteWarn(
                    "GameSession.cpp inserts/erases story flag not set by JSON "
                    "storyEvents/interactions/examineFlag: "
                    + flag);
            }
            setFlags.insert(flag);
        }
    }
    else
    {
        noteWarn("GameSession.cpp not found at " + gameSessionPath + " — skipped C++ scan");
    }

    for (const std::string& flag : setFlags)
    {
        if (readFlags.count(flag) == 0)
            noteWarn("flag set but never read in JSON: " + flag);
    }
    for (const std::string& flag : readFlags)
    {
        if (setFlags.count(flag) == 0)
            noteWarn("flag required/read but never set in JSON data or GameSession.cpp: " + flag);
    }

    std::cout << "Story-events lint: " << errors << " error(s), " << warnings
              << " warning(s).\n";
    if (errors > 0)
        return 1;
    if (warningsAreErrors && warnings > 0)
        return 1;
    return 0;
}
