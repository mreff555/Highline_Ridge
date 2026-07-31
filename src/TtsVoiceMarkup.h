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

#ifndef TTS_VOICE_MARKUP_H
#define TTS_VOICE_MARKUP_H

#include <string>
#include <vector>

namespace timberline_engine
{

struct TtsVoiceSegment
{
    std::string voiceId;
    std::string text;
};

/** Owner-level TTS policy (scene, item, or combine recipe). Off by default. */
struct TtsOwnerPolicy
{
    bool enabled = false;
    std::string defaultVoice;
};

enum class TtsHighlightKind
{
    Default,
    Command,
    VoiceMarkup,
    VoiceDialog,
    VoiceDialogError
};

const std::vector<std::string>& builtinVoiceIds();

std::string formatBuiltinVoiceList();

bool isKnownBuiltinVoiceId(const std::string& voiceId);

std::string normalizeVoiceId(const std::string& voiceId);

/** Explicit error when TTS is on without a valid default voice. */
std::string formatTtsEnabledMissingVoiceError(
    const std::string& ownerLabel,
    const char* defaultVoiceKeyName = "ttsDefaultVoice");

/** Parse JSON owner fields: ttsEnabled + ttsDefaultVoice. */
void parseTtsOwnerPolicyFromJsonFields(
    bool ttsEnabledField,
    const std::string& ttsDefaultVoiceField,
    TtsOwnerPolicy& out);

/**
 * Parse {{voice}} markup into segments.
 * defaultVoiceId must be a known builtin voice (no silent Leo fallback).
 */
bool parseVoiceMarkup(
    const std::string& text,
    const std::string& defaultVoiceId,
    std::vector<TtsVoiceSegment>& outSegments,
    std::string& outError);

/**
 * Classify each character for editor syntax highlighting.
 * Only allowlisted bracket tags are marked Command:
 * [pause], [long-pause], [hum-tune], [laugh], [chuckle], [giggle], [cry],
 * [tsk], [tongue-click], [lip-smack], [breath], [inhale], [exhale], [sigh].
 * All other text (including {{voice}} markup) stays Default.
 */
void classifyTtsTextHighlight(
    const std::string& text,
    std::vector<TtsHighlightKind>& outKinds);

std::string buildSegmentAudioPath(
    const std::string& baseAudioPath,
    size_t segmentIndex,
    size_t segmentCount);

std::vector<std::string> buildSegmentAudioPaths(
    const std::string& baseAudioPath,
    size_t segmentCount);

}

#endif /* TTS_VOICE_MARKUP_H */
