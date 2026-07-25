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

#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <UiBackdrop.h>
#include <string>

namespace timberline_engine
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