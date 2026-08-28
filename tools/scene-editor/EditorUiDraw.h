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

#ifndef TIMBERLINE_EDITOR_UI_DRAW_H
#define TIMBERLINE_EDITOR_UI_DRAW_H
#include "EditorTypes.h"
#include "TtsVoiceMarkup.h"

#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_editor
{

void drawWrappedText(
    Font font,
    const std::string& text,
    Vector2 position,
    float maxWidth,
    float fontSize,
    float lineSpacing,
    Color color);

float measureUiTextWidth(Font font, const std::string& text, float fontSize);

/**
 * Word-wrap layout for caret/click mapping. Each line stores buffer [start, end)
 * indices (end may equal start for empty hard-newline rows).
 */
std::vector<EditorVisualLine> layoutWrappedTextLines(
    Font font,
    const std::string& buffer,
    float maxTextWidth,
    float fontSize);

int visualLineIndexForCursor(const std::vector<EditorVisualLine>& lines, int cursor, int bufferSize);

float caretXOnVisualLine(
    Font font,
    const EditorVisualLine& line,
    int cursor,
    float fontSize);

/** Map a mouse position in a padded field to a buffer cursor index. */
int cursorIndexFromClick(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    const std::string& buffer,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    float scrollY,
    Vector2 mouse);

void drawVisualTextLines(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    float scrollY,
    Color color);

/** Draw visual lines with per-byte colors (size must match buffer). */
void drawVisualTextLinesColored(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    const std::vector<Color>& colors,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    float scrollY,
    Color fallback);

/** Shared TTS syntax colors (from resources/editor_tts_theme.json). */
struct TtsSyntaxThemeColors
{
    Color defaultColor{220, 212, 196, 255};
    Color command{70, 190, 100, 255};        // green — [pause]
    Color styleMarkup{230, 140, 50, 255};    // orange — <whisper>
    Color styleContent{50, 80, 170, 255};    // dark blue — angle content
    Color voiceMarkup{235, 210, 70, 255};    // yellow — {{voice:eve}}
    Color voiceDialog{140, 195, 235, 255};   // light blue — brace content
    Color markupError{220, 55, 55, 255};     // red — unclosed
};

/**
 * Load (once) resources/editor_tts_theme.json from resourceDir.
 * Safe to call repeatedly; first successful/failed attempt sticks.
 */
void ensureTtsSyntaxThemeLoaded(const std::string& resourceDir);

const TtsSyntaxThemeColors& ttsSyntaxTheme();

/** Map TTS highlight kinds using the shared theme. */
Color ttsHighlightKindColor(timberline_engine::TtsHighlightKind kind);

void buildTtsHighlightColors(
    const std::string& text,
    std::vector<Color>& outColors);

/** Move caret vertically by one visual line; preserves preferred X when possible. */
int moveCursorVertical(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    const std::string& buffer,
    int cursor,
    int direction,
    float fontSize,
    float& preferredX);

} // namespace timberline_editor

#endif
