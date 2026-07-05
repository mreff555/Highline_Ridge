#include "TtsVoiceMarkup.h"

#include <cctype>

namespace highline_ridge
{

namespace
{

std::string trimWhitespaceLocal(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(start, end - start);
}

void appendSegmentText(
    std::vector<TtsVoiceSegment>& segments,
    const std::string& voiceId,
    const std::string& text)
{
    const std::string trimmed = trimWhitespaceLocal(text);
    if (trimmed.empty())
        return;

    if (!segments.empty() && segments.back().voiceId == voiceId)
        segments.back().text += trimmed;
    else
        segments.push_back({ voiceId, trimmed });
}

}

bool isKnownBuiltinVoiceId(const std::string& voiceId)
{
    const std::string normalized = normalizeVoiceId(voiceId);
    return normalized == "eve"
        || normalized == "ara"
        || normalized == "rex"
        || normalized == "sal"
        || normalized == "leo";
}

std::string normalizeVoiceId(const std::string& voiceId)
{
    std::string normalized;
    normalized.reserve(voiceId.size());
    for (char character : voiceId)
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return normalized;
}

bool parseVoiceMarkup(
    const std::string& text,
    const std::string& defaultVoiceId,
    std::vector<TtsVoiceSegment>& outSegments,
    std::string& outError)
{
    outSegments.clear();
    outError.clear();

    const std::string defaultVoice = normalizeVoiceId(
        defaultVoiceId.empty() ? "leo" : defaultVoiceId);
    std::string currentVoice = defaultVoice;
    std::string pendingText;
    size_t position = 0;

    auto flushPendingText = [&]()
    {
        appendSegmentText(outSegments, currentVoice, pendingText);
        pendingText.clear();
    };

    while (position < text.size())
    {
        const size_t tagStart = text.find("{{", position);
        if (tagStart == std::string::npos)
        {
            pendingText.append(text.substr(position));
            break;
        }

        pendingText.append(text.substr(position, tagStart - position));

        const size_t tagEnd = text.find("}}", tagStart + 2);
        if (tagEnd == std::string::npos)
        {
            outError = "Unclosed {{voice}} tag in ttsText";
            return false;
        }

        const std::string tagBody = trimWhitespaceLocal(text.substr(tagStart + 2, tagEnd - tagStart - 2));
        position = tagEnd + 2;

        if (tagBody.empty())
        {
            outError = "Empty voice tag in ttsText";
            return false;
        }

        if (tagBody[0] == '/')
        {
            const std::string closeName = normalizeVoiceId(tagBody.substr(1));
            if (closeName == "voice" || isKnownBuiltinVoiceId(closeName))
            {
                flushPendingText();
                currentVoice = defaultVoice;
                continue;
            }

            outError = "Unknown closing voice tag: {{/" + tagBody.substr(1) + "}}";
            return false;
        }

        const std::string voicePrefix = "voice:";
        if (tagBody.rfind(voicePrefix, 0) == 0)
        {
            const std::string voiceName = trimWhitespaceLocal(tagBody.substr(voicePrefix.size()));
            if (voiceName.empty())
            {
                outError = "Missing voice id in {{voice:...}} tag";
                return false;
            }

            flushPendingText();
            currentVoice = normalizeVoiceId(voiceName);
            continue;
        }

        if (isKnownBuiltinVoiceId(tagBody))
        {
            flushPendingText();
            currentVoice = normalizeVoiceId(tagBody);
            continue;
        }

        outError = "Unknown voice tag: {{" + tagBody + "}}";
        return false;
    }

    flushPendingText();

    if (outSegments.empty())
    {
        outError = "No speakable text found in ttsText";
        return false;
    }

    return true;
}

std::string buildSegmentAudioPath(
    const std::string& baseAudioPath,
    size_t segmentIndex,
    size_t segmentCount)
{
    if (segmentCount <= 1)
        return baseAudioPath;

    const size_t dot = baseAudioPath.rfind('.');
    if (dot == std::string::npos)
        return baseAudioPath + ".seg" + std::to_string(segmentIndex);

    return baseAudioPath.substr(0, dot) + ".seg" + std::to_string(segmentIndex)
        + baseAudioPath.substr(dot);
}

std::vector<std::string> buildSegmentAudioPaths(
    const std::string& baseAudioPath,
    size_t segmentCount)
{
    std::vector<std::string> paths;
    paths.reserve(segmentCount == 0 ? 1 : segmentCount);
    const size_t count = segmentCount == 0 ? 1 : segmentCount;
    for (size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex)
        paths.push_back(buildSegmentAudioPath(baseAudioPath, segmentIndex, count));
    return paths;
}

}