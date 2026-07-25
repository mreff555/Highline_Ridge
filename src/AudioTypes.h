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

#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <map>
#include <string>
#include <vector>

namespace timberline_engine
{

enum class AudioCategory
{
    Music,
    Ambient,
    Sfx
};

struct AudioClipDef
{
    std::string path;
    float volume = 1.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
    bool loop = true;
    std::string trigger;
    std::map<std::string, float> numericAttributes;
    std::map<std::string, bool> boolAttributes;
    std::map<std::string, std::string> stringAttributes;
};

struct RoomAudioConfig
{
    AudioClipDef music;
    bool hasMusic = false;
    std::vector<AudioClipDef> ambient;
    std::vector<AudioClipDef> sfx;
};

}

#endif /* AUDIO_TYPES_H */