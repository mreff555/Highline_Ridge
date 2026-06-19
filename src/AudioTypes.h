#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <map>
#include <string>
#include <vector>

namespace testgame
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