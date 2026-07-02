#include "XaiTtsClient.h"

#include "TextDigest.h"
#include <ImageCompression.h>
#include <nlohmann/json.hpp>
#include <raylib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace highline_ridge
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

std::string lookupStoredSha256(
    const nlohmann::json& node,
    const char* audioField)
{
    if (std::strcmp(audioField, "ttsAudio") == 0)
        return node.value("ttsTextSha256", "");
    if (std::strcmp(audioField, "ttsAfterAudio") == 0)
        return node.value("ttsAfterTextSha256", "");
    if (std::strcmp(audioField, "resumeTtsAudio") == 0)
        return node.value("resumeTtsTextSha256", "");
    if (std::strcmp(audioField, "ttsVariantAudio") == 0)
        return node.value("ttsVariantTextSha256", "");
    return "";
}

bool updateSha256InJsonTree(
    nlohmann::json& node,
    const std::string& audioPath,
    const std::string& textSha256)
{
    bool updated = false;

    if (node.is_object())
    {
        if (node.value("ttsAudio", "") == audioPath)
        {
            node["ttsTextSha256"] = textSha256;
            updated = true;
        }
        if (node.value("ttsAfterAudio", "") == audioPath)
        {
            node["ttsAfterTextSha256"] = textSha256;
            updated = true;
        }
        if (node.value("resumeTtsAudio", "") == audioPath)
        {
            node["resumeTtsTextSha256"] = textSha256;
            updated = true;
        }
        if (node.value("ttsVariantAudio", "") == audioPath)
        {
            node["ttsVariantTextSha256"] = textSha256;
            updated = true;
        }

        for (auto it = node.begin(); it != node.end(); ++it)
            updated = updateSha256InJsonTree(*it, audioPath, textSha256) || updated;
    }
    else if (node.is_array())
    {
        for (nlohmann::json& child : node)
            updated = updateSha256InJsonTree(child, audioPath, textSha256) || updated;
    }

    return updated;
}

bool writeJsonFileIfChanged(
    const std::string& path,
    nlohmann::json& root,
    bool changed)
{
    if (!changed)
        return true;

    std::ofstream out(path.c_str());
    if (!out.is_open())
        return false;

    out << root.dump(2);
    return out.good();
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
    const char* flagField,
    const nlohmann::json* sha256Node = nullptr)
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

    const nlohmann::json& sha256Source = sha256Node != nullptr ? *sha256Node : node;
    if (sha256Node != nullptr
        && node.value(audioField, "") == sha256Node->value("resumeTtsAudio", ""))
    {
        entry.storedTextSha256 = sha256Source.value("resumeTtsTextSha256", "");
    }
    else if (sha256Node != nullptr
        && node.value(audioField, "") == sha256Node->value("ttsVariantAudio", ""))
    {
        entry.storedTextSha256 = sha256Source.value("ttsVariantTextSha256", "");
    }
    else
    {
        entry.storedTextSha256 = lookupStoredSha256(sha256Source, audioField);
    }

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
    const std::string& fallbackText,
    const nlohmann::json* sha256Node = nullptr)
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
        "tts",
        sha256Node);
}

void addAfterTtsEntry(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId,
    const nlohmann::json* sha256Node = nullptr)
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
        "ttsAfter",
        sha256Node);
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

void collectSceneNarrativeEntries(
    std::vector<TtsVoiceEntry>& entries,
    const nlohmann::json& node,
    const std::string& defaultVoiceId)
{
    if (!node.is_object())
        return;

    if (node.contains("descriptionTts") && node["descriptionTts"].is_object())
    {
        addPrimaryTtsEntry(
            entries,
            node["descriptionTts"],
            defaultVoiceId,
            node.value("description", ""));
    }

    if (node.contains("examineTts") && node["examineTts"].is_object())
    {
        addPrimaryTtsEntry(
            entries,
            node["examineTts"],
            defaultVoiceId,
            node.value("examineDetails", ""));
    }

    if (node.contains("wakeTts") && node["wakeTts"].is_object())
    {
        addPrimaryTtsEntry(
            entries,
            node["wakeTts"],
            defaultVoiceId,
            node.value("wakeNarrative", ""));
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

        collectSceneNarrativeEntries(entries, sceneIt.value(), defaultVoiceId);

        const nlohmann::json& subScenes = sceneIt.value().value("subScenes", nlohmann::json::object());
        if (subScenes.is_object())
        {
            for (auto subSceneIt = subScenes.begin(); subSceneIt != subScenes.end(); ++subSceneIt)
                collectSceneNarrativeEntries(entries, subSceneIt.value(), defaultVoiceId);
        }

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
                addEntryIfPresent(
                    entries,
                    variantNode,
                    defaultVoiceId,
                    interaction.value("ttsVariantText", ""),
                    "ttsAudio",
                    "ttsText",
                    "ttsVoice",
                    true,
                    "tts",
                    &interaction);
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
        << "  --refresh-voices=API_KEY   Download changed dialog TTS lines from x.ai,\n"
        << "                             save them as resources/audio/tts/*.mp3.xz,\n"
        << "                             and exit. Lines whose ttsText hash already\n"
        << "                             matches the bundled audio are skipped.\n"
        << "                             The API key is not stored.\n"
        << "  -force, --force            With --refresh-voices, ignore stored text\n"
        << "                             hashes and regenerate every dialog line.\n\n"
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
                            phase.value("resumeIntro", ""),
                            &phase);
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

VoiceBundleResult XaiTtsClient::bundleVoiceFile(
    const std::string& apiKey,
    const TtsVoiceEntry& entry,
    const std::string& assetRoot,
    bool forceRefresh)
{
    VoiceBundleResult result;
    result.textSha256 = sha256Hex(entry.text);

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
    const bool bundleExists = FileExists(bundledXzPath.c_str());
    if (!forceRefresh
        && bundleExists
        && !entry.storedTextSha256.empty()
        && entry.storedTextSha256 == result.textSha256)
    {
        TraceLog(LOG_INFO, "TTS text unchanged, skipping: %s", bundledXzPath.c_str());
        result.success = true;
        result.skipped = true;
        return result;
    }

    if (forceRefresh)
        TraceLog(LOG_INFO, "TTS force refresh: %s", bundledXzPath.c_str());
    else if (bundleExists && entry.storedTextSha256.empty())
        TraceLog(LOG_INFO, "TTS bundle missing text hash, refreshing: %s", bundledXzPath.c_str());
    else if (!bundleExists)
        TraceLog(LOG_INFO, "TTS bundle missing, refreshing: %s", bundledXzPath.c_str());
    else
        TraceLog(LOG_INFO, "TTS text changed, refreshing: %s", bundledXzPath.c_str());

    const std::string tempMp3Path = resolvedAudioPath + ".tmp.mp3";
    std::remove(tempMp3Path.c_str());

    if (!synthesizeToFile(apiKey, entry.text, entry.voiceId, tempMp3Path))
        return result;

    std::vector<unsigned char> mp3Bytes;
    if (!loadAssetBytesFromFile(tempMp3Path, mp3Bytes) || mp3Bytes.empty())
    {
        std::remove(tempMp3Path.c_str());
        return result;
    }

    if (!compressBytesToXzFile(mp3Bytes.data(), mp3Bytes.size(), bundledXzPath))
    {
        std::remove(tempMp3Path.c_str());
        return result;
    }

    std::remove(tempMp3Path.c_str());
    std::remove(resolvedAudioPath.c_str());
    TraceLog(LOG_INFO, "Saved TTS bundle: %s", bundledXzPath.c_str());
    result.success = true;
    result.regenerated = true;
    return result;
}

bool XaiTtsClient::persistVoiceTextSha256(
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& audioPath,
    const std::string& textSha256)
{
    bool persisted = false;

    if (!conversationsPath.empty())
    {
        std::ifstream in(conversationsPath.c_str());
        if (in.is_open())
        {
            nlohmann::json conversations;
            try
            {
                in >> conversations;
                if (updateSha256InJsonTree(conversations, audioPath, textSha256)
                    && writeJsonFileIfChanged(conversationsPath, conversations, true))
                    persisted = true;
            }
            catch (const nlohmann::json::exception&)
            {
            }
        }
    }

    if (!scenesPath.empty())
    {
        std::ifstream in(scenesPath.c_str());
        if (in.is_open())
        {
            nlohmann::json scenesRoot;
            try
            {
                in >> scenesRoot;
                if (updateSha256InJsonTree(scenesRoot, audioPath, textSha256)
                    && writeJsonFileIfChanged(scenesPath, scenesRoot, true))
                    persisted = true;
            }
            catch (const nlohmann::json::exception&)
            {
            }
        }
    }

    return persisted;
}

int XaiTtsClient::refreshBundledVoices(
    const std::string& apiKey,
    const std::string& assetRoot,
    const std::string& conversationsPath,
    const std::string& scenesPath,
    const std::string& defaultVoiceId,
    bool forceRefresh)
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
    int skippedCount = 0;
    int regeneratedCount = 0;
    for (const TtsVoiceEntry& entry : entries)
    {
        const VoiceBundleResult bundleResult =
            bundleVoiceFile(trimmedKey, entry, assetRoot, forceRefresh);
        if (!bundleResult.success)
        {
            std::cerr << "Failed to refresh: " << entry.audioPath << "\n";
            continue;
        }

        ++successCount;
        if (bundleResult.skipped)
        {
            ++skippedCount;
            continue;
        }

        ++regeneratedCount;
        if (!persistVoiceTextSha256(
                conversationsPath,
                scenesPath,
                entry.audioPath,
                bundleResult.textSha256))
        {
            std::cerr << "Warning: refreshed audio but failed to persist text hash for "
                      << entry.audioPath << "\n";
        }
    }

    std::cout << "Refreshed " << regeneratedCount << " voice line(s), skipped "
              << skippedCount << " unchanged, " << successCount << " / "
              << entries.size() << " total into resources/audio/tts/*.mp3.xz\n";
    return successCount == static_cast<int>(entries.size()) ? 0 : 2;
}

}