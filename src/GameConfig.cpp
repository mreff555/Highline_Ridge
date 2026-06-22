#include "GameConfig.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace testgame
{

namespace
{

float clampVolume(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

float clampClickHoldSeconds(float value)
{
    if (value < 0.05f)
        return 0.05f;
    if (value > 0.5f)
        return 0.5f;
    return value;
}

}

bool loadGameConfig(const std::string& configPath, GameConfig& outConfig)
{
    std::ifstream file(configPath.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    if (!config.is_object())
        return false;

    if (config.contains("display") && config["display"].is_object())
    {
        const nlohmann::json& display = config["display"];
        outConfig.display.width = display.value("width", outConfig.display.width);
        outConfig.display.height = display.value("height", outConfig.display.height);
        outConfig.display.fullscreen = display.value("fullscreen", outConfig.display.fullscreen);
    }

    if (config.contains("audio") && config["audio"].is_object())
    {
        const nlohmann::json& audio = config["audio"];
        outConfig.audio.master = clampVolume(audio.value("master", outConfig.audio.master));
        outConfig.audio.music = clampVolume(audio.value("music", outConfig.audio.music));
        outConfig.audio.ambient = clampVolume(audio.value("ambient", outConfig.audio.ambient));
        outConfig.audio.sfx = clampVolume(audio.value("sfx", outConfig.audio.sfx));
    }

    if (config.contains("input") && config["input"].is_object())
    {
        const nlohmann::json& input = config["input"];
        outConfig.input.clickHoldSeconds = clampClickHoldSeconds(
            input.value("clickHoldSeconds", outConfig.input.clickHoldSeconds));
        outConfig.input.skipDropConfirmation = input.value(
            "skipDropConfirmation", outConfig.input.skipDropConfirmation);
    }

    if (config.contains("tts") && config["tts"].is_object())
    {
        const nlohmann::json& tts = config["tts"];
        outConfig.tts.enabled = tts.value("enabled", outConfig.tts.enabled);
        outConfig.tts.voiceId = tts.value("voice", outConfig.tts.voiceId);
        outConfig.tts.bundleDir = tts.value("bundleDir", outConfig.tts.bundleDir);
    }

    if (config.contains("saves") && config["saves"].is_object())
    {
        const nlohmann::json& saves = config["saves"];
        outConfig.saves.maxNamedSaves = saves.value(
            "maxNamedSaves",
            saves.value("max_number_of_saves", outConfig.saves.maxNamedSaves));
        if (outConfig.saves.maxNamedSaves < 0)
            outConfig.saves.maxNamedSaves = 0;
    }

    return outConfig.display.width > 0 && outConfig.display.height > 0;
}

bool saveGameConfig(const std::string& configPath, const GameConfig& config)
{
    nlohmann::json root;
    std::ifstream in(configPath.c_str());
    if (in.is_open())
    {
        try
        {
            in >> root;
        }
        catch (const nlohmann::json::exception&)
        {
            root = nlohmann::json::object();
        }
    }

    if (!root.is_object())
        root = nlohmann::json::object();

    root["display"] = {
        {"width", config.display.width},
        {"height", config.display.height},
        {"fullscreen", config.display.fullscreen}
    };
    root["audio"] = {
        {"master", config.audio.master},
        {"music", config.audio.music},
        {"ambient", config.audio.ambient},
        {"sfx", config.audio.sfx}
    };
    root["input"] = {
        {"clickHoldSeconds", config.input.clickHoldSeconds},
        {"skipDropConfirmation", config.input.skipDropConfirmation}
    };
    root["saves"] = {
        {"maxNamedSaves", config.saves.maxNamedSaves}
    };

    std::ofstream out(configPath.c_str());
    if (!out.is_open())
        return false;

    out << root.dump(2);
    return out.good();
}

}