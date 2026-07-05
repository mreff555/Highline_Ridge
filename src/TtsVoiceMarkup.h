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

std::string buildSegmentAudioPath(
    const std::string& baseAudioPath,
    size_t segmentIndex,
    size_t segmentCount);

std::vector<std::string> buildSegmentAudioPaths(
    const std::string& baseAudioPath,
    size_t segmentCount);

}

#endif /* TTS_VOICE_MARKUP_H */