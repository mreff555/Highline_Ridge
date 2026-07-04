#ifndef XAI_TTS_CLIENT_H
#define XAI_TTS_CLIENT_H

#include <GameConfig.h>
#include <string>
#include <vector>

namespace highline_ridge
{

struct TtsVoiceEntry
{
    std::string audioPath;
    std::string text;
    std::string voiceId;
    std::string storedTextSha256;
};

struct VoiceBundleResult
{
    bool success = false;
    bool skipped = false;
    bool regenerated = false;
    std::string textSha256;
};

class XaiTtsClient
{
    public:
    static std::vector<TtsVoiceEntry> collectVoiceEntries(
        const std::string& conversationsPath,
        const std::string& scenesPath,
        const std::string& defaultVoiceId,
        const std::string& refreshFilter = "");

    static int refreshBundledVoices(
        const std::string& apiKey,
        const std::string& assetRoot,
        const std::string& conversationsPath,
        const std::string& scenesPath,
        const std::string& defaultVoiceId,
        bool forceRefresh = false,
        const std::string& refreshFilter = "");

    private:
    static bool synthesizeToFile(
        const std::string& apiKey,
        const std::string& text,
        const std::string& voiceId,
        const std::string& outputPath);

    static VoiceBundleResult bundleVoiceFile(
        const std::string& apiKey,
        const TtsVoiceEntry& entry,
        const std::string& assetRoot,
        bool forceRefresh = false);

    static bool persistVoiceTextSha256(
        const std::string& conversationsPath,
        const std::string& scenesPath,
        const std::string& audioPath,
        const std::string& textSha256);
};

void printGameHelp(const char* executableName);

}

#endif /* XAI_TTS_CLIENT_H */