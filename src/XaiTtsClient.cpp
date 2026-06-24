#include "XaiTtsClient.h"

#include <ImageCompression.h>
#include <nlohmann/json.hpp>
#include <raylib.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace testgame
{

namespace
{

std::string curlExecutable()
{
#if defined(__APPLE__)
    return "/usr/bin/curl";
#else
    return "curl";
#endif
}

std::string trimWhitespace(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(start, end - start);
}

std::string flattenNarrationText(const std::string& value)
{
    std::string flattened;
    flattened.reserve(value.size());

    bool previousWasSpace = true;
    for (char character : value)
    {
        if (character == '\n' || character == '\r' || character == '\t')
            character = ' ';

        const bool isSpace = std::isspace(static_cast<unsigned char>(character)) != 0;
        if (isSpace)
        {
            if (!previousWasSpace)
                flattened.push_back(' ');
            previousWasSpace = true;
            continue;
        }

        flattened.push_back(character);
        previousWasSpace = false;
    }

    while (!flattened.empty() && flattened.back() == ' ')
        flattened.pop_back();

    return flattened;
}

void addEntryIfPresent(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId,
    const std::string& fallbackText,
    const char* audioField,
    const char* textField,
    const char* voiceField,
    bool requiredFlag,
    const char* flagField)
{
    if (!node.is_object())
        return;

    if (requiredFlag && !node.value(flagField, false))
        return;

    TtsVoiceEntry entry;
    entry.audioPath = node.value(audioField, "");
    entry.text = node.value(textField, "");
    entry.voiceId = node.value(voiceField, defaultVoiceId);
    if (entry.voiceId.empty())
        entry.voiceId = defaultVoiceId;

    if (entry.text.empty())
        entry.text = flattenNarrationText(fallbackText);

    if (entry.audioPath.empty() || entry.text.empty())
        return;

    for (const TtsVoiceEntry& existing : entries)
    {
        if (existing.audioPath == entry.audioPath)
            return;
    }

    entries.push_back(entry);
}

void addPrimaryTtsEntry(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId,
    const std::string& fallbackText)
{
    addEntryIfPresent(
        entries,
        node,
        defaultVoiceId,
        fallbackText,
        "ttsAudio",
        "ttsText",
        "ttsVoice",
        true,
        "tts");
}

void addAfterTtsEntry(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId)
{
    addEntryIfPresent(
        entries,
        node,
        defaultVoiceId,
        "",
        "ttsAfterAudio",
        "ttsAfterText",
        "ttsAfterVoice",
        true,
        "ttsAfter");
}

void collectChoiceEntries(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& choices,
    const std::string& defaultVoiceId)
{
    if (!choices.is_array())
        return;

    for (const nlohmann::json& choice : choices)
    {
        addPrimaryTtsEntry(entries, choice, defaultVoiceId, choice.value("response", ""));
        addAfterTtsEntry(entries, choice, defaultVoiceId);
        if (choice.contains("choices"))
            collectChoiceEntries(entries, choice["choices"], defaultVoiceId);
    }
}

void collectRandomLineEntries(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& lines,
    const std::string& defaultVoiceId)
{
    if (!lines.is_array())
        return;

    for (const nlohmann::json& line : lines)
    {
        addPrimaryTtsEntry(entries, line, defaultVoiceId, line.value("text", ""));
        addAfterTtsEntry(entries, line, defaultVoiceId);
        collectChoiceEntries(entries, line.value("choices", nlohmann::json::array()), defaultVoiceId);
    }
}

void collectSceneInteractionEntries(
    std::vector<TtsVoiceEntry>& entries,
    const std::string& scenesPath,
    const std::string& defaultVoiceId)
{
    std::ifstream file(scenesPath.c_str());
    if (!file.is_open())
        return;

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::exception&)
    {
        return;
    }

    const nlohmann::json& scenes = config.value("scenes", nlohmann::json::object());
    if (!scenes.is_object())
        return;

    for (auto sceneIt = scenes.begin(); sceneIt != scenes.end(); ++sceneIt)
    {
        if (!sceneIt.value().is_object())
            continue;

        const nlohmann::json& interactions = sceneIt.value().value("interactions", nlohmann::json::array());
        if (!interactions.is_array())
            continue;

        for (const nlohmann::json& interaction : interactions)
        {
            addPrimaryTtsEntry(
                entries,
                interaction,
                defaultVoiceId,
                interaction.value("useDetails", ""));
            addAfterTtsEntry(entries, interaction, defaultVoiceId);

            if (interaction.value("tts", false) && !interaction.value("ttsVariantAudio", "").empty())
            {
                nlohmann::json variantNode = nlohmann::json::object();
                variantNode["tts"] = true;
                variantNode["ttsAudio"] = interaction.value("ttsVariantAudio", "");
                variantNode["ttsText"] = interaction.value("ttsVariantText", "");
                variantNode["ttsVoice"] = interaction.value("ttsVoice", defaultVoiceId);
                addPrimaryTtsEntry(
                    entries,
                    variantNode,
                    defaultVoiceId,
                    interaction.value("ttsVariantText", ""));
            }
        }
    }
}

}

void printGameHelp(const char* executableName)
{
    const char* programName = (executableName != nullptr && executableName[0] != '\0')
        ? executableName
        : "Highline Ridge";

    std::cout
        << "Highline Ridge\n\n"
        << "Usage:\n"
        << "  \"" << programName << "\" [options]\n\n"
        << "Options:\n"
        << "  -h, --help                 Show this help message\n"
        << "  --refresh-voices=API_KEY   Download all dialog TTS lines from x.ai,\n"
        << "                             save them as resources/audio/tts/*.mp3.xz,\n"
        << "                             and exit. The API key is not stored.\n\n"
        << "Normal play uses bundled voice files and does not require an API key.\n";
}

std::vector<TtsVoiceEntry> XaiTtsClient::collectVoiceEntries(
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& defaultVoiceId)
{
    std::vector<TtsVoiceEntry> entries;

    std::ifstream file(conversationsPath.c_str());
    if (file.is_open())
    {
        nlohmann::json conversations;
        try
        {
            file >> conversations;
        }
        catch (const nlohmann::json::exception&)
        {
            conversations = nlohmann::json::object();
        }

        if (conversations.is_object())
        {
            for (auto sceneIt = conversations.begin(); sceneIt != conversations.end(); ++sceneIt)
            {
                if (!sceneIt.value().is_object())
                    continue;

                const nlohmann::json& phases = sceneIt.value().value("speakPhases", nlohmann::json::array());
                if (!phases.is_array())
                    continue;

                for (const nlohmann::json& phase : phases)
                {
                    addPrimaryTtsEntry(
                        entries,
                        phase,
                        defaultVoiceId,
                        phase.value("intro", phase.value("text", "")));
                    addAfterTtsEntry(entries, phase, defaultVoiceId);

                    if (phase.value("resumeTts", false))
                    {
                        nlohmann::json resumeNode = nlohmann::json::object();
                        resumeNode["tts"] = true;
                        resumeNode["ttsAudio"] = phase.value("resumeTtsAudio", "");
                        resumeNode["ttsText"] = phase.value("resumeTtsText", "");
                        resumeNode["ttsVoice"] = phase.value("resumeTtsVoice", defaultVoiceId);
                        addPrimaryTtsEntry(
                            entries,
                            resumeNode,
                            defaultVoiceId,
                            phase.value("resumeIntro", ""));
                    }

                    collectChoiceEntries(entries, phase.value("choices", nlohmann::json::array()), defaultVoiceId);
                    collectRandomLineEntries(entries, phase.value("lines", nlohmann::json::array()), defaultVoiceId);
                }
            }
        }
    }

    if (!scenesPath.empty())
        collectSceneInteractionEntries(entries, scenesPath, defaultVoiceId);

    return entries;
}

bool XaiTtsClient::synthesizeToFile(
    const std::string& apiKey,
    const std::string& text,
    const std::string& voiceId,
    const std::string& outputPath)
{
    if (apiKey.empty() || text.empty() || outputPath.empty())
        return false;

    if (!ensureParentDirectoryExists(outputPath))
        return false;

    nlohmann::json payload;
    payload["text"] = text;
    payload["voice_id"] = voiceId;
    payload["language"] = "en";

    const std::string payloadPath = outputPath + ".json";
    {
        std::ofstream payloadFile(payloadPath.c_str());
        if (!payloadFile.is_open())
            return false;
        payloadFile << payload.dump();
        if (!payloadFile.good())
            return false;
    }

    std::ostringstream command;
    command << curlExecutable() << " -sS --fail-with-body -X POST https://api.x.ai/v1/tts "
            << "-H \"Authorization: Bearer " << apiKey << "\" "
            << "-H \"Content-Type: application/json\" "
            << "-d @\"" << payloadPath << "\" "
            << "--output \"" << outputPath << "\"";
    const int exitCode = std::system(command.str().c_str());
    std::remove(payloadPath.c_str());

    if (exitCode != 0 || !FileExists(outputPath.c_str()))
    {
        std::remove(outputPath.c_str());
        return false;
    }

    return true;
}

bool XaiTtsClient::bundleVoiceFile(
    const std::string& apiKey,
    const TtsVoiceEntry& entry,
    const std::string& assetRoot,
    bool forceRefresh)
{
    const std::vector<std::string> audioPaths = buildAssetSearchPaths(assetRoot, entry.audioPath);
    std::string resolvedAudioPath;
    for (const std::string& path : audioPaths)
    {
        if (FileExists(path.c_str()))
        {
            resolvedAudioPath = path;
            break;
        }
    }
    if (resolvedAudioPath.empty())
        resolvedAudioPath = resolveAssetPath(assetRoot, entry.audioPath);

    const std::string bundledXzPath = compressedAssetPath(resolvedAudioPath);
    if (!forceRefresh && FileExists(bundledXzPath.c_str()))
    {
        TraceLog(LOG_INFO, "TTS bundle up to date: %s", bundledXzPath.c_str());
        return true;
    }

    const std::string tempMp3Path = resolvedAudioPath + ".tmp.mp3";
    std::remove(tempMp3Path.c_str());

    TraceLog(LOG_INFO, "Refreshing TTS bundle: %s", bundledXzPath.c_str());
    if (!synthesizeToFile(apiKey, entry.text, entry.voiceId, tempMp3Path))
        return false;

    std::vector<unsigned char> mp3Bytes;
    if (!loadAssetBytesFromFile(tempMp3Path, mp3Bytes) || mp3Bytes.empty())
    {
        std::remove(tempMp3Path.c_str());
        return false;
    }

    if (!compressBytesToXzFile(mp3Bytes.data(), mp3Bytes.size(), bundledXzPath))
    {
        std::remove(tempMp3Path.c_str());
        return false;
    }

    std::remove(tempMp3Path.c_str());
    std::remove(resolvedAudioPath.c_str());
    TraceLog(LOG_INFO, "Saved TTS bundle: %s", bundledXzPath.c_str());
    return true;
}

int XaiTtsClient::refreshBundledVoices(
    const std::string& apiKey,
    const std::string& assetRoot,
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& defaultVoiceId)
{
    const std::string trimmedKey = trimWhitespace(apiKey);
    if (trimmedKey.empty())
    {
        std::cerr << "Missing API key. Use --refresh-voices=YOUR_XAI_API_KEY\n";
        return 1;
    }

    const std::vector<TtsVoiceEntry> entries =
        collectVoiceEntries(conversationsPath, scenesPath, defaultVoiceId);
    if (entries.empty())
    {
        std::cerr << "No TTS entries found in " << conversationsPath << "\n";
        return 1;
    }

    int successCount = 0;
    for (const TtsVoiceEntry& entry : entries)
    {
        if (bundleVoiceFile(trimmedKey, entry, assetRoot, true))
            ++successCount;
        else
            std::cerr << "Failed to refresh: " << entry.audioPath << "\n";
    }

    std::cout << "Refreshed " << successCount << " / " << entries.size()
              << " voice line(s) into resources/audio/tts/*.mp3.xz\n";
    return successCount == static_cast<int>(entries.size()) ? 0 : 2;
}

}