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

#include "TtsVoiceMarkup.h"

#include <cctype>

namespace timberline_engine
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

bool isTtsCommandBodyChar(unsigned char ch)
{
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
}

/** Exact allowlist of bracket tags eligible for editor syntax highlighting. */
bool isAllowlistedTtsCommandTag(const std::string& body)
{
    // Normalize: trim + lowercase so [Pause] / " long-pause " still match.
    std::string normalized;
    normalized.reserve(body.size());
    size_t begin = 0;
    while (begin < body.size() && std::isspace(static_cast<unsigned char>(body[begin])))
        ++begin;
    size_t end = body.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(body[end - 1])))
        --end;
    for (size_t i = begin; i < end; ++i)
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(body[i]))));

    static const char* kAllowed[] = {
        "pause",
        "long-pause",
        "hum-tune",
        "laugh",
        "chuckle",
        "giggle",
        "cry",
        "tsk",
        "tongue-click",
        "lip-smack",
        "breath",
        "inhale",
        "exhale",
        "sigh",
    };
    for (const char* tag : kAllowed)
    {
        if (normalized == tag)
            return true;
    }
    return false;
}

bool looksLikeTtsCommandBody(const std::string& body)
{
    return isAllowlistedTtsCommandTag(body);
}

// Classify open tag body: returns true if it is a voice-open form.
// knownVoice: true only when the voice id is recognized.
bool classifyVoiceOpenTag(const std::string& body, bool& knownVoice, std::string& voiceIdOut)
{
    knownVoice = false;
    voiceIdOut.clear();
    const std::string trimmed = trimWhitespaceLocal(body);
    if (trimmed.empty() || trimmed[0] == '/')
        return false;

    const std::string lower = normalizeVoiceId(trimmed);
    const std::string voicePrefix = "voice:";
    if (lower.rfind(voicePrefix, 0) == 0)
    {
        const std::string voiceName = trimWhitespaceLocal(trimmed.substr(voicePrefix.size()));
        if (voiceName.empty())
            return true; // open form, but missing id → unknown
        voiceIdOut = normalizeVoiceId(voiceName);
        knownVoice = isKnownBuiltinVoiceId(voiceIdOut);
        return true;
    }

    if (isKnownBuiltinVoiceId(lower))
    {
        voiceIdOut = lower;
        knownVoice = true;
        return true;
    }

    // Looks like a short-form voice tag but unknown (single token, no spaces).
    if (lower.find_first_of(" \t\r\n") == std::string::npos && !lower.empty())
    {
        voiceIdOut = lower;
        knownVoice = false;
        return true;
    }

    return false;
}

bool classifyVoiceCloseTag(const std::string& body, bool& knownClose)
{
    knownClose = false;
    const std::string trimmed = trimWhitespaceLocal(body);
    if (trimmed.empty() || trimmed[0] != '/')
        return false;

    const std::string name = normalizeVoiceId(trimWhitespaceLocal(trimmed.substr(1)));
    if (name == "voice" || isKnownBuiltinVoiceId(name))
    {
        knownClose = true;
        return true;
    }

    // Close form with unknown name
    return !name.empty();
}

}

const std::vector<std::string>& builtinVoiceIds()
{
    static const std::vector<std::string> kVoices = {
        "ara", "eve", "leo", "rex", "sal"
    };
    return kVoices;
}

std::string formatBuiltinVoiceList()
{
    const std::vector<std::string>& voices = builtinVoiceIds();
    std::string list;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        if (i > 0)
            list += ", ";
        list += voices[i];
    }
    return list;
}

bool isKnownBuiltinVoiceId(const std::string& voiceId)
{
    const std::string normalized = normalizeVoiceId(voiceId);
    for (const std::string& voice : builtinVoiceIds())
    {
        if (voice == normalized)
            return true;
    }
    return false;
}

std::string normalizeVoiceId(const std::string& voiceId)
{
    std::string normalized;
    normalized.reserve(voiceId.size());
    for (char character : voiceId)
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return normalized;
}

std::string formatTtsEnabledMissingVoiceError(
    const std::string& ownerLabel,
    const char* defaultVoiceKeyName)
{
    const char* key = (defaultVoiceKeyName != nullptr && defaultVoiceKeyName[0] != '\0')
        ? defaultVoiceKeyName
        : "ttsDefaultVoice";

    return ownerLabel
        + " is TTS enabled and requires a valid default voice entry(\""
        + key
        + "\":=\"<valid default voice>\"). You could also choose to disable TTS for that scene "
          "(or item / recipe) by setting ttsEnabled to false. "
          "Current voices available are: "
        + formatBuiltinVoiceList();
}

void parseTtsOwnerPolicyFromJsonFields(
    bool ttsEnabledField,
    const std::string& ttsDefaultVoiceField,
    TtsOwnerPolicy& out)
{
    out.enabled = ttsEnabledField;
    out.defaultVoice = normalizeVoiceId(ttsDefaultVoiceField);
}

bool parseVoiceMarkup(
    const std::string& text,
    const std::string& defaultVoiceId,
    std::vector<TtsVoiceSegment>& outSegments,
    std::string& outError)
{
    outSegments.clear();
    outError.clear();

    if (defaultVoiceId.empty())
    {
        outError = "Default voice is empty; a known builtin voice is required";
        return false;
    }

    const std::string defaultVoice = normalizeVoiceId(defaultVoiceId);
    if (!isKnownBuiltinVoiceId(defaultVoice))
    {
        outError = "Unknown default voice \"" + defaultVoiceId
            + "\". Current voices available are: " + formatBuiltinVoiceList();
        return false;
    }

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

            const std::string normalized = normalizeVoiceId(voiceName);
            if (!isKnownBuiltinVoiceId(normalized))
            {
                outError = "Unknown voice \"" + voiceName
                    + "\" in {{voice:" + voiceName + "}}. Current voices available are: "
                    + formatBuiltinVoiceList();
                return false;
            }

            flushPendingText();
            currentVoice = normalized;
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

void classifyTtsTextHighlight(
    const std::string& text,
    std::vector<TtsHighlightKind>& outKinds)
{
    outKinds.assign(text.size(), TtsHighlightKind::Default);
    if (text.empty())
        return;

    // Editor highlighting is intentionally limited to the allowlisted
    // bracket tags (pause, laugh, sigh, …). Voice {{…}} markup is not
    // colorized here so authors only see the approved command set.
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '[')
        {
            const size_t close = text.find(']', i + 1);
            if (close != std::string::npos)
            {
                const std::string body = text.substr(i + 1, close - (i + 1));
                if (looksLikeTtsCommandBody(body))
                {
                    for (size_t j = i; j <= close; ++j)
                        outKinds[j] = TtsHighlightKind::Command;
                    i = close + 1;
                    continue;
                }
            }
        }
        ++i;
    }
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
