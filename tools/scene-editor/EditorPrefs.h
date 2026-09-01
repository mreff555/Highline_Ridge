/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Editor-only preferences (not packed into release assets).
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_PREFS_H
#define TIMBERLINE_EDITOR_PREFS_H

#include <string>
#include <vector>

namespace timberline_editor
{

/** Default world-style filter for new installs / empty prefs. */
inline constexpr const char* kDefaultGenerationStyleFilter =
    "Colorado high-altitude mountains; 1891 frontier Colorado; period-accurate clothing "
    "and tools; painterly realistic light; no modern objects; no UI text";

/**
 * Preferred TTS default voice for new authoring prompts.
 * Order: resources/editor_prefs.json lastTtsDefaultVoice →
 *        game_config.json tts.voice → "leo".
 */
std::string preferredTtsDefaultVoice(const std::string& resourceDir);

/** Persist last-chosen default voice into resources/editor_prefs.json. */
bool rememberTtsDefaultVoice(const std::string& resourceDir, const std::string& voiceId);

/** Semicolon-delimited world style filter (generation consistency). */
std::string loadGenerationStyleFilter(const std::string& resourceDir);

bool saveGenerationStyleFilter(const std::string& resourceDir, const std::string& filter);

/** Split filter on ';' → trimmed non-empty clauses (order preserved). */
std::vector<std::string> parseGenerationStyleClauses(const std::string& filter);

/**
 * Format clauses as a prompt block for Grok image/chat jobs.
 * Empty filter → empty string (caller keeps only hard-coded period rules).
 */
std::string formatGenerationStyleBlock(const std::string& filter);

} // namespace timberline_editor

#endif
