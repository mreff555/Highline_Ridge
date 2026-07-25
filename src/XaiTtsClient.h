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

#ifndef XAI_TTS_CLIENT_H
#define XAI_TTS_CLIENT_H

#include <GameConfig.h>
#include <string>
#include <vector>

namespace timberline_engine
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
    std::vector<std::string> segmentAudioPaths;
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

    static bool persistVoiceAudioSegments(
        const std::string& conversationsPath,
        const std::string& scenesPath,
        const std::string& audioPath,
        const std::vector<std::string>& segmentPaths);
};

void printGameHelp(const char* executableName);

}

#endif /* XAI_TTS_CLIENT_H */