#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <string>

namespace testgame
{

struct AudioVolumeConfig
{
    float master = 1.0f;
    float music = 1.0f;
    float ambient = 1.0f;
    float sfx = 1.0f;
};

struct DisplayConfig
{
    int width = 1500;
    int height = 1117;
    bool fullscreen = false;
};

struct InputConfig
{
    float clickHoldSeconds = 0.1f;
    bool skipDropConfirmation = false;
};

struct TtsConfig
{
    bool enabled = true;
    std::string voiceId = "leo";
    std::string bundleDir = "resources/audio/tts";
};

struct SaveConfig
{
    int maxNamedSaves = 10;
};

struct GameConfig
{
    DisplayConfig display;
    AudioVolumeConfig audio;
    InputConfig input;
    TtsConfig tts;
    SaveConfig saves;
};

bool loadGameConfig(const std::string& configPath, GameConfig& outConfig);
bool saveGameConfig(const std::string& configPath, const GameConfig& config);

}

#endif /* GAME_CONFIG_H */