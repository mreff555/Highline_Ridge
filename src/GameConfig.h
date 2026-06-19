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
};

struct GameConfig
{
    DisplayConfig display;
    AudioVolumeConfig audio;
};

bool loadGameConfig(const std::string& configPath, GameConfig& outConfig);

}

#endif /* GAME_CONFIG_H */