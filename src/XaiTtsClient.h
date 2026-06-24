#ifndef XAI_TTS_CLIENT_H
#define XAI_TTS_CLIENT_H

#include <GameConfig.h>
#include <string>
#include <vector>

namespace testgame
{

struct TtsVoiceEntry
{
    std::string audioPath;
    std::string text;
    std::string voiceId;
};

class XaiTtsClient
{
    public:
    static std::vector<TtsVoiceEntry> collectVoiceEntries(
        const std::string& conversationsPath,
        const std::string& scenesPath,
        const std::string& defaultVoiceId);

    static int refreshBundledVoices(
        const std::string& apiKey,
        const std::string& assetRoot,
        const std::string& conversationsPath,
        const std::string& scenesPath,
        const std::string& defaultVoiceId);

    private:
    static bool synthesizeToFile(
        const std::string& apiKey,
        const std::string& text,
        const std::string& voiceId,
        const std::string& outputPath);

    static bool bundleVoiceFile(
        const std::string& apiKey,
        const TtsVoiceEntry& entry,
        const std::string& assetRoot,
        bool forceRefresh);
};

void printGameHelp(const char* executableName);

}

#endif /* XAI_TTS_CLIENT_H */