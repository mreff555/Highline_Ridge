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
    }

    if (config.contains("audio") && config["audio"].is_object())
    {
        const nlohmann::json& audio = config["audio"];
        outConfig.audio.master = clampVolume(audio.value("master", outConfig.audio.master));
        outConfig.audio.music = clampVolume(audio.value("music", outConfig.audio.music));
        outConfig.audio.ambient = clampVolume(audio.value("ambient", outConfig.audio.ambient));
        outConfig.audio.sfx = clampVolume(audio.value("sfx", outConfig.audio.sfx));
    }

    return outConfig.display.width > 0 && outConfig.display.height > 0;
}

}