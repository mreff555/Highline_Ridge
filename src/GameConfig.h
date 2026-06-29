#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <UiBackdrop.h>
#include <string>

namespace highline_ridge
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
    int x = -1;
    int y = -1;
    int monitor = -1;
};

struct InputConfig
{
    float clickHoldSeconds = 0.07f;
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
    UiConfig ui;
};

bool loadGameConfig(const std::string& configPath, GameConfig& outConfig);
bool saveGameConfig(const std::string& configPath, const GameConfig& config);
bool displayConfigsEqual(const DisplayConfig& left, const DisplayConfig& right);
void syncDisplayConfigFromWindow(DisplayConfig& display);
void applySavedWindowPlacement(const DisplayConfig& display);

}

#endif /* GAME_CONFIG_H */