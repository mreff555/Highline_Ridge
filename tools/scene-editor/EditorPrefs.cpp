/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "EditorPrefs.h"
#include "PlatformPath.h"
#include "TtsVoiceMarkup.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <sstream>

using timberline_engine::isKnownBuiltinVoiceId;
using timberline_engine::normalizeVoiceId;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

std::string prefsPath(const std::string& resourceDir)
{
    return pathJoin(resourceDir, "editor_prefs.json");
}

nlohmann::json loadPrefsObject(const std::string& resourceDir)
{
    nlohmann::json prefs = nlohmann::json::object();
    std::ifstream in(prefsPath(resourceDir).c_str());
    if (!in.is_open())
        return prefs;
    try
    {
        in >> prefs;
        if (!prefs.is_object())
            prefs = nlohmann::json::object();
    }
    catch (const nlohmann::json::exception&)
    {
        prefs = nlohmann::json::object();
    }
    return prefs;
}

bool writePrefsObject(const std::string& resourceDir, const nlohmann::json& prefs)
{
    std::ofstream out(prefsPath(resourceDir).c_str());
    if (!out)
        return false;
    out << prefs.dump(2) << '\n';
    return static_cast<bool>(out);
}

std::string voiceFromGameConfig(const std::string& resourceDir)
{
    const std::string configPath = pathJoin(resourceDir, "game_config.json");
    std::ifstream file(configPath.c_str());
    if (!file.is_open())
        return "";

    try
    {
        nlohmann::json config;
        file >> config;
        if (!config.is_object() || !config.contains("tts") || !config["tts"].is_object())
            return "";
        return normalizeVoiceId(config["tts"].value("voice", ""));
    }
    catch (const nlohmann::json::exception&)
    {
        return "";
    }
}

std::string trimCopy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

} // namespace

std::string preferredTtsDefaultVoice(const std::string& resourceDir)
{
    const nlohmann::json prefs = loadPrefsObject(resourceDir);
    const std::string voice =
        normalizeVoiceId(prefs.value("lastTtsDefaultVoice", ""));
    if (!voice.empty() && isKnownBuiltinVoiceId(voice))
        return voice;

    const std::string fromConfig = voiceFromGameConfig(resourceDir);
    if (!fromConfig.empty() && isKnownBuiltinVoiceId(fromConfig))
        return fromConfig;
    return "leo";
}

bool rememberTtsDefaultVoice(const std::string& resourceDir, const std::string& voiceId)
{
    const std::string voice = normalizeVoiceId(voiceId);
    if (voice.empty() || !isKnownBuiltinVoiceId(voice))
        return false;

    nlohmann::json prefs = loadPrefsObject(resourceDir);
    prefs["lastTtsDefaultVoice"] = voice;
    return writePrefsObject(resourceDir, prefs);
}

std::string loadGenerationStyleFilter(const std::string& resourceDir)
{
    const nlohmann::json prefs = loadPrefsObject(resourceDir);
    if (prefs.contains("generationStyleFilter") && prefs["generationStyleFilter"].is_string())
        return prefs["generationStyleFilter"].get<std::string>();
    return kDefaultGenerationStyleFilter;
}

bool saveGenerationStyleFilter(const std::string& resourceDir, const std::string& filter)
{
    nlohmann::json prefs = loadPrefsObject(resourceDir);
    prefs["generationStyleFilter"] = filter;
    return writePrefsObject(resourceDir, prefs);
}

std::vector<std::string> parseGenerationStyleClauses(const std::string& filter)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : filter)
    {
        if (ch == ';')
        {
            const std::string t = trimCopy(cur);
            if (!t.empty())
                out.push_back(t);
            cur.clear();
        }
        else
            cur.push_back(ch);
    }
    const std::string t = trimCopy(cur);
    if (!t.empty())
        out.push_back(t);
    return out;
}

std::string formatGenerationStyleBlock(const std::string& filter)
{
    const std::vector<std::string> clauses = parseGenerationStyleClauses(filter);
    if (clauses.empty())
        return {};
    std::ostringstream oss;
    oss << "World / casting / tone constraints (always honor; earlier items are stronger):\n";
    for (const std::string& c : clauses)
        oss << "- " << c << "\n";
    return oss.str();
}

} // namespace timberline_editor
