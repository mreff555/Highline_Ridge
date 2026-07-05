#ifndef TTS_VOICE_MARKUP_H
#define TTS_VOICE_MARKUP_H

#include <string>
#include <vector>

namespace highline_ridge
{

struct TtsVoiceSegment
{
    std::string voiceId;
    std::string text;
};

bool isKnownBuiltinVoiceId(const std::string& voiceId);

std::string normalizeVoiceId(const std::string& voiceId);

bool parseVoiceMarkup(
    const std::string& text,
    const std::string& defaultVoiceId,
    std::vector<TtsVoiceSegment>& outSegments,
    std::string& outError);

}

#endif /* TTS_VOICE_MARKUP_H */