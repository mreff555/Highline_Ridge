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

#ifndef TTS_CONTENT_VALIDATOR_H
#define TTS_CONTENT_VALIDATOR_H

#include <string>
#include <vector>

namespace timberline_engine
{

struct TtsValidationIssue
{
    std::string message;
};

/**
 * Validate TTS policies and voice markup across resource JSON files.
 * Fails hard on: TTS-enabled owner without known default voice; invalid/unclosed markup
 * on eligible TTS text fields under an enabled owner.
 */
bool validateTtsResources(
    const std::string& scenesPath,
    const std::string& conversationsPath,
    const std::string& itemsPath,
    const std::string& combinationsPath,
    std::vector<TtsValidationIssue>& outIssues);

/** Convenience: validate and append issues to stderr / TraceLog via return false. */
bool validateTtsResourcesOrLog(
    const std::string& scenesPath,
    const std::string& conversationsPath,
    const std::string& itemsPath,
    const std::string& combinationsPath);

}

#endif /* TTS_CONTENT_VALIDATOR_H */
