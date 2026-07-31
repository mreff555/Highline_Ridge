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

#include "VariableEditor.h"

#include "ConversationHelpers.h"
#include "DocumentWorkspace.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "RaylibCompat.h"
#include "SceneDocument.h"
#include "ThumbnailCache.h"
#include "TtsVoiceMarkup.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <filesystem>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using timberline_engine::SceneActor;
using timberline_engine::SceneDocument;
using timberline_engine::SceneLayout;
using timberline_engine::TtsHighlightKind;
using timberline_engine::builtinVoiceIds;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::classifyTtsTextHighlight;
using timberline_engine::compressedAssetPath;
using timberline_engine::isKnownBuiltinVoiceId;
using timberline_engine::listDirectoryFileNames;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::normalizeVoiceId;
using timberline_engine::pathJoin;

namespace fs = std::filesystem;

namespace timberline_editor
{

void VariableEditor::closeVariableEditor()
{
    open = false;
    docTarget = ConversationEditDoc::None;
    jsonPointer.clear();
    editorSceneId.clear();
    editorItemId.clear();
    editorKey.clear();
    buffer.clear();
    textTtsEnabled = false;
    showTts = false;
    textSideBuffer.clear();
    ttsSideBuffer.clear();
    textTtsMode = TextTtsPairMode::None;
    ttsObjectKey.clear();
    cursor = 0;
    selectAnchor = -1;
    mouseSelecting = false;
    lastClickTime = -1.0;
    lastClickPos = -1;
    scrollY = 0.0f;
    error.clear();
    keyRepeatKey = 0;
    keyRepeatTimer = 0.0f;
    textTtsToggle = {0, 0, 0, 0};
    voiceDropdownOpen = false;
    voiceDropdownBtn = {0, 0, 0, 0};
    voiceDropdownMenu = {0, 0, 0, 0};
}


bool VariableEditor::splitJsonPointer(
    const std::string& pointer,
    std::string& parentOut,
    std::string& leafOut)
{
    if (pointer.empty() || pointer[0] != '/')
        return false;
    const size_t last = pointer.find_last_of('/');
    if (last == std::string::npos)
        return false;
    parentOut = pointer.substr(0, last);
    leafOut = unescapeJsonPointerToken(pointer.substr(last + 1));
    return !leafOut.empty();
}


nlohmann::json* VariableEditor::resolveEditorParentObject(const std::string& parentPointer)
{
    if (docTarget == ConversationEditDoc::Conversations)
    {
        if (parentPointer.empty())
            return docs->conversationsLoaded ? &docs->conversationsRoot : nullptr;
        return docs->conversationJsonAt(parentPointer);
    }
    if (docTarget == ConversationEditDoc::Items)
    {
        if (parentPointer.empty())
            return docs->itemJson(editorItemId);
        return docs->itemFieldAt(editorItemId, parentPointer);
    }
    // Scenes document and plain scene-variable editors share scene JSON roots.
    if (docTarget == ConversationEditDoc::Scenes
        || docTarget == ConversationEditDoc::None)
    {
        if (parentPointer.empty())
            return docs->scenes.sceneJson(editorSceneId);
        return docs->sceneFieldAt(editorSceneId, parentPointer);
    }
    return nullptr;
}


void VariableEditor::syncActiveBufferFromSide()
{
    buffer = showTts ? ttsSideBuffer : textSideBuffer;
    if (textTtsMode == TextTtsPairMode::ObjectSplit)
        kind = VariableKindJson;
    else
        kind = VariableKindString;
    multiline = true;
    cursor = static_cast<int>(buffer.size());
    selectAnchor = -1;
    mouseSelecting = false;
    scrollY = 0.0f;
    error.clear();
}


void VariableEditor::stashActiveBufferToSide()
{
    if (showTts)
        ttsSideBuffer = buffer;
    else
        textSideBuffer = buffer;
}


bool VariableEditor::variableHasSelection() const
{
    return selectAnchor >= 0 &&
        selectAnchor != cursor;
}


void VariableEditor::variableSelectionRange(int& outStart, int& outEnd) const
{
    outStart = std::min(selectAnchor, cursor);
    outEnd = std::max(selectAnchor, cursor);
    if (outStart < 0)
        outStart = 0;
    if (outEnd < 0)
        outEnd = 0;
    if (outEnd > static_cast<int>(buffer.size()))
        outEnd = static_cast<int>(buffer.size());
    if (outStart > outEnd)
        outStart = outEnd;
}


void VariableEditor::clearVariableSelection()
{
    selectAnchor = -1;
}


bool VariableEditor::deleteVariableSelection()
{
    if (!variableHasSelection())
        return false;
    int start = 0;
    int end = 0;
    variableSelectionRange(start, end);
    buffer.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
    cursor = start;
    clearVariableSelection();
    return true;
}


void VariableEditor::setVariableCursor(int pos, bool extendSelection)
{
    clampVariableCursor();
    if (pos < 0)
        pos = 0;
    if (pos > static_cast<int>(buffer.size()))
        pos = static_cast<int>(buffer.size());

    if (extendSelection)
    {
        if (selectAnchor < 0)
            selectAnchor = cursor;
    }
    else
    {
        clearVariableSelection();
    }
    cursor = pos;
}


bool VariableEditor::editorIsWordChar(unsigned char ch)
{
    // ASCII word characters plus UTF-8 continuation / lead bytes so multi-byte
    // letters stay part of the same "word" for selection.
    if (ch >= 0x80)
        return true;
    return std::isalnum(ch) != 0 || ch == '_' || ch == '\'';
}


void VariableEditor::selectWordAtCursor(int pos)
{
    const int n = static_cast<int>(buffer.size());
    if (n <= 0)
    {
        selectAnchor = 0;
        cursor = 0;
        return;
    }

    if (pos < 0)
        pos = 0;
    if (pos > n)
        pos = n;

    // Click past end of a word: select the word to the left when possible.
    int at = pos;
    if (at >= n || !editorIsWordChar(static_cast<unsigned char>(buffer[static_cast<size_t>(at)])))
    {
        if (at > 0 &&
            editorIsWordChar(static_cast<unsigned char>(buffer[static_cast<size_t>(at - 1)])))
        {
            at = at - 1;
        }
        else
        {
            // Non-word: select a single character (or nothing at EOF).
            if (at >= n)
            {
                selectAnchor = n;
                cursor = n;
                return;
            }
            selectAnchor = at;
            cursor = at + 1;
            return;
        }
    }

    int start = at;
    while (start > 0 &&
           editorIsWordChar(static_cast<unsigned char>(
               buffer[static_cast<size_t>(start - 1)])))
    {
        --start;
    }

    int end = at + 1;
    while (end < n &&
           editorIsWordChar(static_cast<unsigned char>(
               buffer[static_cast<size_t>(end)])))
    {
        ++end;
    }

    selectAnchor = start;
    cursor = end;
}


void VariableEditor::openVariableEditor(const std::string& sceneId, const std::string& key)
{
    const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->contains(key))
    {
        TraceLog(LOG_WARNING, "TIMBERLINE: cannot edit missing key %s", key.c_str());
        return;
    }

    const nlohmann::json& value = (*scene)[key];
    docTarget = ConversationEditDoc::None;
    // Point at the top-level scene field so Text/TTS + parent bag resolution work.
    jsonPointer = "/" + key;
    textTtsEnabled = false;
    showTts = false;
    textTtsMode = TextTtsPairMode::None;
    textSideBuffer.clear();
    ttsSideBuffer.clear();
    ttsObjectKey.clear();
    editorSceneId = sceneId;
    editorKey = key;
    scrollY = 0.0f;
    error.clear();
    selectedVariableKey = key;
    TraceLog(LOG_INFO, "TIMBERLINE: editing %s.%s", sceneId.c_str(), key.c_str());

    // Copy by value immediately so we never hold a dangling json reference.
    if (value.is_string())
    {
        kind = VariableKindString;
        buffer = value.get<std::string>();
    }
    else if (value.is_boolean())
    {
        kind = VariableKindBool;
        buffer = value.get<bool>() ? "true" : "false";
    }
    else if (value.is_number_integer())
    {
        kind = VariableKindInteger;
        buffer = std::to_string(value.get<long long>());
    }
    else if (value.is_number_float())
    {
        kind = VariableKindFloat;
        std::ostringstream stream;
        stream << value.get<double>();
        buffer = stream.str();
    }
    else if (value.is_null())
    {
        kind = VariableKindString;
        buffer.clear();
    }
    else
    {
        kind = VariableKindJson;
        buffer = value.dump(2);
    }

    multiline =
        kind == VariableKindJson ||
        buffer.find('\n') != std::string::npos ||
        buffer.size() > 80;
    // Enable Text/TTS for scene narrative fields edited from the variables pane.
    ensureGlobalDefaultVoiceLoaded();
    ensureTtsSyntaxThemeLoaded();
    setupTextTtsForOpenedValue(value);
    if (!textTtsEnabled)
    {
        cursor = static_cast<int>(buffer.size());
        selectAnchor = -1;
        mouseSelecting = false;
    }
    open = true;
    // One frame only — long enough to ignore the activating click, not laggy.
    ignoreInputFrames = 1;
}


bool VariableEditor::applyEditorBufferToJson(nlohmann::json& value)
{
    try
    {
        if (kind == VariableKindBool)
        {
            std::string lowered = buffer;
            for (size_t i = 0; i < lowered.size(); ++i)
                lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
            if (lowered == "true" || lowered == "1" || lowered == "yes")
                value = true;
            else if (lowered == "false" || lowered == "0" || lowered == "no")
                value = false;
            else
                return false;
        }
        else if (kind == VariableKindInteger)
        {
            value = std::stoll(buffer);
        }
        else if (kind == VariableKindFloat)
        {
            value = std::stod(buffer);
        }
        else if (kind == VariableKindJson)
        {
            value = nlohmann::json::parse(buffer);
        }
        else
        {
            value = buffer;
        }
    }
    catch (...)
    {
        return false;
    }
    return true;
}


bool VariableEditor::saveVariableEditor()
{
    if (docTarget == ConversationEditDoc::Conversations)
    {
        if (textTtsEnabled)
        {
            if (!applyTextTtsSidesToDocument())
                return false;
        }
        else
        {
            nlohmann::json* target = docs->conversationJsonAt(jsonPointer);
            if (target == nullptr)
                return false;
            if (!applyEditorBufferToJson(*target))
                return false;
        }

        docs->markDirty();
        if (!docs->saveConversationsDocument())
        {
            error = "Applied in memory, but failed to write conversations.json";
            return false;
        }
        docs->dirty = false;
        if (onTreeRebuild) onTreeRebuild();
        closeVariableEditor();
        return true;
    }

    if (docTarget == ConversationEditDoc::Scenes)
    {
        if (textTtsEnabled)
        {
            if (!applyTextTtsSidesToDocument())
                return false;
        }
        else
        {
            nlohmann::json* target = docs->sceneFieldAt(editorSceneId, jsonPointer);
            if (target == nullptr)
                return false;
            if (!applyEditorBufferToJson(*target))
                return false;
        }

        docs->markDirty();
        if (!docs->scenes.save())
        {
            error = "Applied in memory, but failed to write scenes.json";
            return false;
        }
        docs->dirty = false;
        if (onTreeRebuild) onTreeRebuild();
        closeVariableEditor();
        return true;
    }

    if (docTarget == ConversationEditDoc::Items)
    {
        if (textTtsEnabled)
        {
            if (!applyTextTtsSidesToDocument())
                return false;
        }
        else
        {
            nlohmann::json* target = docs->itemFieldAt(editorItemId, jsonPointer);
            if (target == nullptr)
                return false;
            if (!applyEditorBufferToJson(*target))
                return false;
        }

        docs->markDirty();
        if (!docs->saveItemsDocument())
        {
            error = "Applied in memory, but failed to write items.json";
            return false;
        }
        docs->dirty = false;
        if (onTreeRebuild) onTreeRebuild();
        closeVariableEditor();
        return true;
    }

    nlohmann::json* scene = docs->scenes.sceneJson(editorSceneId);
    if (scene == nullptr)
        return false;

    if (textTtsEnabled && docTarget == ConversationEditDoc::None)
    {
        if (!applyTextTtsSidesToDocument())
            return false;
        docs->markDirty();
        if (!docs->scenes.save())
        {
            error = "Applied in memory, but failed to write scenes.json";
            return false;
        }
        docs->dirty = false;
        if (onTreeRebuild) onTreeRebuild();
        closeVariableEditor();
        return true;
    }

    nlohmann::json& value = (*scene)[editorKey];
    if (!applyEditorBufferToJson(value))
        return false;

    docs->markDirty();
    // Persist immediately so Save in the popup has an obvious effect.
    if (!docs->scenes.save())
    {
        error = "Applied in memory, but failed to write scenes.json";
        return false;
    }
    docs->dirty = false;
    closeVariableEditor();
    return true;
}


void VariableEditor::drawEditorLineText(
    const EditorVisualLine& line,
    float x,
    float y,
    float fontSize,
    bool highlightTts) const
{
    if (line.text.empty())
        return;

    if (!highlightTts)
    {
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            line.text.c_str(),
            {x, y},
            fontSize,
            1.0f,
            kTextPrimary);
        return;
    }

    float drawX = x;
    size_t i = 0;
    while (i < line.text.size())
    {
        const int bufIdx = line.start + static_cast<int>(i);
        const Color runColor = ttsColorAtBufferIndex(bufIdx);
        size_t j = i + 1;
        while (j < line.text.size() &&
               VariableEditor::colorsEqual(ttsColorAtBufferIndex(line.start + static_cast<int>(j)), runColor))
        {
            ++j;
        }

        const std::string run = line.text.substr(i, j - i);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            run.c_str(),
            {drawX, y},
            fontSize,
            1.0f,
            runColor);
        drawX += measureUiTextWidth(run, fontSize);
        i = j;
    }
}


void VariableEditor::clampVariableCursor()
{
    if (cursor < 0)
        cursor = 0;
    if (cursor > static_cast<int>(buffer.size()))
        cursor = static_cast<int>(buffer.size());
}


int VariableEditor::utf8PrevIndex(int cursor) const
{
    if (cursor <= 0)
        return 0;
    int at = cursor - 1;
    while (at > 0 &&
           (static_cast<unsigned char>(buffer[static_cast<size_t>(at)]) & 0xC0) == 0x80)
    {
        --at;
    }
    return at;
}


int VariableEditor::utf8NextIndex(int cursor) const
{
    if (cursor >= static_cast<int>(buffer.size()))
        return static_cast<int>(buffer.size());
    int at = cursor + 1;
    while (at < static_cast<int>(buffer.size()) &&
           (static_cast<unsigned char>(buffer[static_cast<size_t>(at)]) & 0xC0) == 0x80)
    {
        ++at;
    }
    return at;
}


const std::vector<EditorVisualLine>& VariableEditor::buildEditorVisualLines(float maxTextWidth, float fontSize) const
{
    if (visualLinesCacheBuffer == buffer &&
        visualLinesCacheMaxW == maxTextWidth &&
        visualLinesCacheFontSize == fontSize &&
        !visualLinesCache.empty())
    {
        return visualLinesCache;
    }

    visualLinesCache.clear();
    visualLinesCacheBuffer = buffer;
    visualLinesCacheMaxW = maxTextWidth;
    visualLinesCacheFontSize = fontSize;

    // Leave a little slack so MeasureTextEx rounding never paints past the pad.
    const float wrapWidth = std::max(8.0f, maxTextWidth - 2.0f);
    int i = 0;
    const int n = static_cast<int>(buffer.size());

    auto utf8Next = [&](int at) -> int
    {
        if (at >= n)
            return n;
        int next = at + 1;
        while (next < n
               && (static_cast<unsigned char>(buffer[static_cast<size_t>(next)]) & 0xC0) == 0x80)
        {
            ++next;
        }
        return next;
    };

    auto isSoftBreakByte = [](unsigned char ch) -> bool
    {
        return ch == ' ' || ch == '\t' || ch == '-' || ch == 0x2D;
    };

    auto pushLine = [&](int start, int end)
    {
        EditorVisualLine line;
        line.start = start;
        line.end = end;
        if (end > start)
            line.text = buffer.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
        else
            line.text.clear();
        visualLinesCache.push_back(line);
    };

    if (n == 0)
    {
        pushLine(0, 0);
        return visualLinesCache;
    }

    while (i < n)
    {
        if (buffer[static_cast<size_t>(i)] == '\n')
        {
            // Empty visual line for a hard newline; caret sits on this row.
            pushLine(i, i);
            ++i;
            continue;
        }

        const int lineStart = i;
        int lastSoftBreakEnd = -1; // buffer index after a soft-break codepoint still in width

        while (i < n && buffer[static_cast<size_t>(i)] != '\n')
        {
            const int next = utf8Next(i);
            const std::string candidate = buffer.substr(
                static_cast<size_t>(lineStart),
                static_cast<size_t>(next - lineStart));
            const float width = measureUiTextWidth(candidate, fontSize);

            if (width > wrapWidth && next > lineStart)
            {
                // Prefer wrapping after the last soft break on this line.
                int wrapEnd = lastSoftBreakEnd;
                if (wrapEnd <= lineStart)
                {
                    // Hard wrap: if the first glyph alone exceeds the width, keep it;
                    // otherwise break before the overflowing codepoint.
                    if (i == lineStart)
                        wrapEnd = next;
                    else
                        wrapEnd = i;
                }

                pushLine(lineStart, wrapEnd);
                i = wrapEnd;
                // Skip a single leading space on the next visual line after a soft wrap.
                if (i < n && buffer[static_cast<size_t>(i)] == ' ')
                    ++i;
                // If wrapEnd skipped soft-break mid-line, continue outer while.
                break;
            }

            // Track soft break *after* this codepoint when it still fits.
            const unsigned char lead =
                static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            if (isSoftBreakByte(lead))
                lastSoftBreakEnd = next;

            i = next;

            if (i >= n || buffer[static_cast<size_t>(i)] == '\n')
            {
                pushLine(lineStart, i);
                break;
            }
        }
    }

    // Trailing newline produces an extra empty line for caret placement.
    if (!buffer.empty() && buffer[buffer.size() - 1] == '\n')
        pushLine(n, n);

    if (visualLinesCache.empty())
        pushLine(0, 0);

    return visualLinesCache;
}


int VariableEditor::editorLineIndexForCursor(const std::vector<EditorVisualLine>& lines, int cursor) const
{
    if (lines.empty())
        return 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const int nextStart = (i + 1 < lines.size())
            ? lines[i + 1].start
            : (static_cast<int>(buffer.size()) + 1);
        if (cursor >= lines[i].start && cursor < nextStart)
            return static_cast<int>(i);
    }
    return static_cast<int>(lines.size()) - 1;
}


float VariableEditor::editorCaretXOnLine(
    const EditorVisualLine& line,
    int cursor,
    float fontSize) const
{
    const int local = std::max(0, std::min(cursor, line.end) - line.start);
    const std::string before = line.text.substr(
        0,
        static_cast<size_t>(std::min(local, static_cast<int>(line.text.size()))));
    return measureUiTextWidth(before, fontSize);
}


int VariableEditor::editorCursorFromClick(
    const std::vector<EditorVisualLine>& lines,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    Vector2 mouse) const
{
    if (lines.empty())
        return 0;

    const float relY = (mouse.y - (field.y + pad) + scrollY) / lineHeight;
    int lineIndex = static_cast<int>(std::floor(relY));
    if (lineIndex < 0)
        lineIndex = 0;
    if (lineIndex >= static_cast<int>(lines.size()))
        lineIndex = static_cast<int>(lines.size()) - 1;

    const EditorVisualLine& line = lines[static_cast<size_t>(lineIndex)];
    const float relX = mouse.x - (field.x + pad);
    if (relX <= 0.0f)
        return line.start;

    int best = line.start;
    float bestDist = relX;
    for (int pos = line.start; pos <= line.end; ++pos)
    {
        // Skip placing mid-UTF-8 sequence.
        if (pos > line.start && pos < line.end &&
            (static_cast<unsigned char>(buffer[static_cast<size_t>(pos)]) & 0xC0) == 0x80)
        {
            continue;
        }
        const float x = editorCaretXOnLine(line, pos, fontSize);
        const float dist = std::fabs(x - relX);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = pos;
        }
    }
    return best;
}


void VariableEditor::ensureCursorVisible(
    const std::vector<EditorVisualLine>& lines,
    float fieldHeight,
    float pad,
    float lineHeight)
{
    if (lines.empty())
        return;
    const int lineIndex = editorLineIndexForCursor(lines, cursor);
    const float caretTop = static_cast<float>(lineIndex) * lineHeight;
    const float caretBottom = caretTop + lineHeight;
    const float viewH = fieldHeight - pad * 2.0f;
    if (caretTop < scrollY)
        scrollY = caretTop;
    if (caretBottom > scrollY + viewH)
        scrollY = caretBottom - viewH;
    if (scrollY < 0.0f)
        scrollY = 0.0f;
}


bool VariableEditor::editorNavKeyTriggered(int key)
{
    // Use IsKeyDown so arrows work on macOS. Fire immediately, then repeat quickly.
    if (!IsKeyDown(key))
    {
        if (keyRepeatKey == key)
        {
            keyRepeatKey = 0;
            keyRepeatTimer = 0.0f;
        }
        return false;
    }

    if (keyRepeatKey != key)
    {
        keyRepeatKey = key;
        keyRepeatTimer = 0.0f;
        return true;
    }

    keyRepeatTimer += GetFrameTime();
    // First event already fired on key-down; wait before auto-repeat.
    const float initialDelay = kKeyRepeatInitialDelaySeconds;
    const float repeatEvery = kKeyRepeatEverySeconds;
    if (keyRepeatTimer < initialDelay)
        return false;
    if (keyRepeatTimer >= initialDelay + repeatEvery)
    {
        keyRepeatTimer = initialDelay;
        return true;
    }
    return false;
}


int VariableEditor::cursorOnLineAtPreferX(const EditorVisualLine& line, float preferX, float fontSize) const
{
    int best = line.start;
    float bestDist = 1.0e9f;
    for (int pos = line.start; pos <= line.end; ++pos)
    {
        if (pos > line.start && pos < line.end &&
            (static_cast<unsigned char>(buffer[static_cast<size_t>(pos)]) & 0xC0) == 0x80)
        {
            continue;
        }
        const float x = editorCaretXOnLine(line, pos, fontSize);
        const float dist = std::fabs(x - preferX);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = pos;
        }
    }
    return best;
}


void VariableEditor::moveVariableCursorVertical(int direction, const std::vector<EditorVisualLine>& lines, float fontSize)
{
    if (lines.empty())
        return;

    const int lineIndex = editorLineIndexForCursor(lines, cursor);
    const EditorVisualLine& cur = lines[static_cast<size_t>(lineIndex)];
    preferX = editorCaretXOnLine(cur, cursor, fontSize);

    const int targetLine = lineIndex + direction;
    if (targetLine < 0)
    {
        cursor = 0;
        return;
    }
    if (targetLine >= static_cast<int>(lines.size()))
    {
        cursor = static_cast<int>(buffer.size());
        return;
    }

    cursor = cursorOnLineAtPreferX(
        lines[static_cast<size_t>(targetLine)],
        preferX,
        fontSize);
}


void VariableEditor::syncDialogLayout(int screenWidth, int screenHeight)
{
    if (!open)
        return;

    const float dialogW = multiline ? 760.0f : 520.0f;
    const float dialogH = multiline ? 520.0f : 190.0f;
    const float dialogX = (static_cast<float>(screenWidth) - dialogW) * 0.5f;
    const float dialogY = (static_cast<float>(screenHeight) - dialogH) * 0.5f;
    const float btnH = 34.0f;
    const float btnW = 110.0f;
    const float btnY = dialogY + dialogH - btnH - 16.0f;

    fieldRect = {
        dialogX + 18.0f,
        dialogY + 44.0f,
        dialogW - 36.0f,
        btnY - (dialogY + 44.0f) - 14.0f};
    saveBtn = {dialogX + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
    cancelBtn = {dialogX + dialogW - btnW - 18.0f, btnY, btnW, btnH};

    if (textTtsEnabled)
    {
        const float toggleW = 56.0f;
        textTtsToggle = {dialogX + 18.0f, btnY, toggleW, btnH};
    }
    else
    {
        textTtsToggle = {0, 0, 0, 0};
    }

    fontSize = kFontBody;
    lineHeight = fontSize + 4.0f;
    pad = 8.0f;
}

void VariableEditor::handleVariableEditorTextInput()
{
    if (!open)
        return;

    if (ignoreInputFrames > 0)
    {
        while (GetCharPressed() > 0)
        {
        }
        // Drain key queue so the activating key cannot act later.
        while (GetKeyPressed() != 0)
        {
        }
        --ignoreInputFrames;
        return;
    }

    if (fieldRect.width <= 1.0f || fieldRect.height <= 1.0f)
        return;

    const Rectangle field = fieldRect;
    const float localPad = pad;
    const float localFontSize = fontSize;
    const float localLineHeight = lineHeight;
    const float maxTextW = field.width - localPad * 2.0f;
    auto lines = [&]() -> const std::vector<EditorVisualLine>&
    {
        return buildEditorVisualLines(maxTextW, localFontSize);
    };
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
        IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    const Vector2 mouse = GetMousePosition();

    // Buttons take priority over the text field (handled here in update).
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, saveBtn))
        {
            mouseSelecting = false;
            if (!saveVariableEditor())
            {
                if (error.empty())
                    error = "Could not parse value — check type and try again";
            }
            return;
        }
        if (CheckCollisionPointRec(mouse, cancelBtn))
        {
            mouseSelecting = false;
            closeVariableEditor();
            return;
        }
        if (textTtsEnabled &&
            textTtsToggle.width > 1.0f &&
            CheckCollisionPointRec(mouse, textTtsToggle))
        {
            mouseSelecting = false;
            toggleTextTtsSide();
            return;
        }

        // Scene Default TTS voice dropdown.
        if (voiceDropdownBtn.width > 1.0f && CheckCollisionPointRec(mouse, voiceDropdownBtn))
        {
            mouseSelecting = false;
            voiceDropdownOpen = !voiceDropdownOpen;
            return;
        }
        if (voiceDropdownOpen && voiceDropdownMenu.height > 1.0f)
        {
            const std::vector<std::string>& voices = builtinVoiceIds();
            const int optionCount = 1 + static_cast<int>(voices.size());
            const float rowH = voiceDropdownMenu.height / static_cast<float>(optionCount);
            bool hit = false;
            for (int i = 0; i < optionCount; ++i)
            {
                const Rectangle row = {
                    voiceDropdownMenu.x,
                    voiceDropdownMenu.y + rowH * static_cast<float>(i),
                    voiceDropdownMenu.width,
                    rowH};
                if (CheckCollisionPointRec(mouse, row))
                {
                    const std::string opt = (i == 0) ? "OFF" : voices[static_cast<size_t>(i - 1)];
                    applyOwnerTtsPolicySelection(opt);
                    voiceDropdownOpen = false;
                    hit = true;
                    break;
                }
            }
            if (!hit && !CheckCollisionPointRec(mouse, voiceDropdownBtn))
                voiceDropdownOpen = false;
            if (hit)
                return;
        }
    }

    // Do not place caret under an open voice menu.
    if (voiceDropdownOpen
        && voiceDropdownMenu.height > 1.0f
        && CheckCollisionPointRec(mouse, voiceDropdownMenu))
        return;

    // Click to place caret; double-click selects word; drag extends selection.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, field))
    {
        const int pos = editorCursorFromClick(
            lines(), field, pad, fontSize, lineHeight, mouse);
        const double now = GetTime();
        const bool isDoubleClick =
            !shift &&
            lastClickTime >= 0.0 &&
            (now - lastClickTime) <= 0.4 &&
            std::abs(pos - lastClickPos) <= 2;

        if (isDoubleClick)
        {
            selectWordAtCursor(pos);
            mouseSelecting = false;
            lastClickTime = -1.0;
            lastClickPos = -1;
        }
        else
        {
            setVariableCursor(pos, shift);
            mouseSelecting = !shift;
            if (!shift)
                selectAnchor = cursor;
            lastClickTime = now;
            lastClickPos = pos;
        }

        const int lineIndex = editorLineIndexForCursor(lines(), cursor);
        preferX = editorCaretXOnLine(
            lines()[static_cast<size_t>(lineIndex)],
            cursor,
            fontSize);
        ensureCursorVisible(lines(), field.height, localPad, localLineHeight);
    }
    else if (mouseSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        // Only extend selection while the pointer is over the field.
        if (CheckCollisionPointRec(mouse, field))
        {
            if (selectAnchor < 0)
                selectAnchor = cursor;
            cursor = editorCursorFromClick(
                lines(), field, pad, fontSize, lineHeight, mouse);
            clampVariableCursor();
            const int lineIndex = editorLineIndexForCursor(lines(), cursor);
            preferX = editorCaretXOnLine(
                lines()[static_cast<size_t>(lineIndex)],
                cursor,
                fontSize);
            ensureCursorVisible(lines(), field.height, localPad, localLineHeight);
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        mouseSelecting = false;

    // Copy / cut / paste / select-all
    if (ctrl && IsKeyPressed(KEY_A))
    {
        selectAnchor = 0;
        cursor = static_cast<int>(buffer.size());
    }
    if (ctrl && IsKeyPressed(KEY_C) && variableHasSelection())
    {
        int start = 0;
        int end = 0;
        variableSelectionRange(start, end);
        SetClipboardText(buffer.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
    }
    if (ctrl && IsKeyPressed(KEY_X) && variableHasSelection())
    {
        int start = 0;
        int end = 0;
        variableSelectionRange(start, end);
        SetClipboardText(buffer.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
        deleteVariableSelection();
    }
    if (ctrl && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            deleteVariableSelection();
            clampVariableCursor();
            const std::string paste(clip);
            buffer.insert(static_cast<size_t>(cursor), paste);
            cursor += static_cast<int>(paste.size());
            clearVariableSelection();
        }
    }

    // Text insertion (replaces selection if present)
    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (codepoint >= 32 && codepoint != 127)
        {
            std::string encoded;
            if (codepoint < 0x80)
            {
                encoded.push_back(static_cast<char>(codepoint));
            }
            else if (codepoint < 0x800)
            {
                encoded.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else
            {
                encoded.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }

            deleteVariableSelection();
            clampVariableCursor();
            buffer.insert(static_cast<size_t>(cursor), encoded);
            cursor += static_cast<int>(encoded.size());
            clearVariableSelection();
        }
        codepoint = GetCharPressed();
    }

    // Arrow navigation (shift extends selection)
    if (editorNavKeyTriggered(KEY_LEFT))
        setVariableCursor(utf8PrevIndex(cursor), shift);
    if (editorNavKeyTriggered(KEY_RIGHT))
        setVariableCursor(utf8NextIndex(cursor), shift);
    if (editorNavKeyTriggered(KEY_UP))
    {
        if (shift && selectAnchor < 0)
            selectAnchor = cursor;
        moveVariableCursorVertical(-1, lines(), fontSize);
        if (!shift)
            clearVariableSelection();
    }
    if (editorNavKeyTriggered(KEY_DOWN))
    {
        if (shift && selectAnchor < 0)
            selectAnchor = cursor;
        moveVariableCursorVertical(1, lines(), fontSize);
        if (!shift)
            clearVariableSelection();
    }

    if (IsKeyPressed(KEY_HOME))
    {
        if (ctrl || !multiline)
            setVariableCursor(0, shift);
        else
        {
            const int lineIndex = editorLineIndexForCursor(lines(), cursor);
            setVariableCursor(lines()[static_cast<size_t>(lineIndex)].start, shift);
        }
    }

    if (IsKeyPressed(KEY_END))
    {
        if (ctrl || !multiline)
            setVariableCursor(static_cast<int>(buffer.size()), shift);
        else
        {
            const int lineIndex = editorLineIndexForCursor(lines(), cursor);
            setVariableCursor(lines()[static_cast<size_t>(lineIndex)].end, shift);
        }
    }

    if (editorNavKeyTriggered(KEY_BACKSPACE))
    {
        if (!deleteVariableSelection() &&
            cursor > 0 && !buffer.empty())
        {
            const int eraseAt = utf8PrevIndex(cursor);
            buffer.erase(
                static_cast<size_t>(eraseAt),
                static_cast<size_t>(cursor - eraseAt));
            cursor = eraseAt;
            clearVariableSelection();
        }
    }

    if (editorNavKeyTriggered(KEY_DELETE))
    {
        if (!deleteVariableSelection() &&
            cursor < static_cast<int>(buffer.size()))
        {
            const int eraseEnd = utf8NextIndex(cursor);
            buffer.erase(
                static_cast<size_t>(cursor),
                static_cast<size_t>(eraseEnd - cursor));
            clearVariableSelection();
        }
    }

    if (multiline && editorNavKeyTriggered(KEY_ENTER))
    {
        deleteVariableSelection();
        clampVariableCursor();
        buffer.insert(static_cast<size_t>(cursor), "\n");
        ++cursor;
        clearVariableSelection();
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeVariableEditor();
        return;
    }

    clampVariableCursor();
    ensureCursorVisible(lines(), field.height, localPad, localLineHeight);
}


void VariableEditor::drawVariableEditor(int screenWidth, int screenHeight)
{
    if (!open)
        return;

    DrawRectangle(0, 0, screenWidth, screenHeight, Color{0, 0, 0, 200});

    const float dialogW = multiline ? 760.0f : 520.0f;
    const float dialogH = multiline ? 520.0f : 190.0f;
    const Rectangle dialog = {
        (static_cast<float>(screenWidth) - dialogW) * 0.5f,
        (static_cast<float>(screenHeight) - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRounded(dialog, 0.03f, 8, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    // Upper-right: Default TTS voice (OFF + all builtin actors). Layout first so the
    // title can be clipped and the open menu can be painted last (above the field).
    const bool showOwnerTtsDropdown =
        (docTarget == ConversationEditDoc::Items && !editorItemId.empty())
        || (!editorSceneId.empty()
            && (docTarget == ConversationEditDoc::Conversations
                || docTarget == ConversationEditDoc::Scenes
                || docTarget == ConversationEditDoc::None));

    Font headerFont = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const char* voiceLabel = "Default TTS voice:";
    const Vector2 voiceLabelSize = MeasureTextEx(headerFont, voiceLabel, kFontTiny, 1.0f);
    // Fixed width: long enough for OFF and every known voice id + chevron.
    float voiceValueW = MeasureTextEx(headerFont, "OFF", kFontBody, 1.0f).x;
    for (const std::string& voiceId : builtinVoiceIds())
        voiceValueW = std::max(
            voiceValueW,
            MeasureTextEx(headerFont, voiceId.c_str(), kFontBody, 1.0f).x);
    const float voiceBtnW = voiceValueW + 36.0f;
    const float voiceBtnH = 26.0f;
    const float voiceBtnX = dialog.x + dialogW - 18.0f - voiceBtnW;
    const float voiceBtnY = dialog.y + 12.0f;
    const float voiceClusterLeft = showOwnerTtsDropdown
        ? (voiceBtnX - voiceLabelSize.x - 12.0f)
        : (dialog.x + dialogW - 18.0f);

    if (showOwnerTtsDropdown)
    {
        const std::string current = ownerTtsPolicyDropdownLabel();
        const Vector2 valueSize = MeasureTextEx(headerFont, current.c_str(), kFontBody, 1.0f);
        voiceDropdownBtn = {voiceBtnX, voiceBtnY, voiceBtnW, voiceBtnH};
        // Precompute menu hit bounds even before the late draw pass so the first
        // open frame is clickable after layout (update uses previous frame's rects).
        const std::vector<std::string>& voices = builtinVoiceIds();
        const int optionCount = 1 + static_cast<int>(voices.size());
        const float rowH = 24.0f;
        if (voiceDropdownOpen)
        {
            voiceDropdownMenu = {
                voiceBtnX,
                voiceBtnY + voiceBtnH + 2.0f,
                voiceBtnW,
                rowH * static_cast<float>(optionCount)};
        }
        else
        {
            voiceDropdownMenu = {0, 0, 0, 0};
        }

        DrawTextEx(
            headerFont,
            voiceLabel,
            {voiceBtnX - voiceLabelSize.x - 8.0f,
             voiceBtnY + (voiceBtnH - voiceLabelSize.y) * 0.5f},
            kFontTiny,
            1.0f,
            kPanelBorder);
        DrawRectangleRec(voiceDropdownBtn, Color{44, 42, 52, 255});
        DrawRectangleLinesEx(voiceDropdownBtn, 1.0f, kPanelBorder);
        DrawTextEx(
            headerFont,
            current.c_str(),
            {voiceBtnX + 8.0f, voiceBtnY + (voiceBtnH - valueSize.y) * 0.5f},
            kFontBody,
            1.0f,
            kTextPrimary);
        DrawTextEx(
            headerFont,
            voiceDropdownOpen ? "^" : "v",
            {voiceBtnX + voiceBtnW - 16.0f, voiceBtnY + 4.0f},
            kFontBody,
            1.0f,
            kPanelBorder);
    }
    else
    {
        voiceDropdownBtn = {0, 0, 0, 0};
        voiceDropdownMenu = {0, 0, 0, 0};
    }

    // Title only — mode is already shown by the lower-left text/TTS switch.
    std::string title = "Edit \"" + editorKey + "\"";
    if (docTarget == ConversationEditDoc::Conversations)
        title = "Edit conversation  —  " + editorKey;
    else if (docTarget == ConversationEditDoc::Scenes)
        title = "Edit narrative  —  " + editorKey + "  (" + editorSceneId + ")";
    else if (docTarget == ConversationEditDoc::Items)
        title = "Edit item  —  " + editorKey + "  (" + editorItemId + ")";
    else if (!editorSceneId.empty())
        title = "Edit \"" + editorKey + "\"  —  scene: " + editorSceneId;

    const float titleMaxW = std::max(40.0f, voiceClusterLeft - (dialog.x + 18.0f) - 8.0f);
    while (title.size() > 4
           && MeasureTextEx(headerFont, title.c_str(), kFontTitle, 1.0f).x > titleMaxW)
    {
        title.resize(title.size() - 1);
    }
    if (title.size() >= 4
        && MeasureTextEx(headerFont, title.c_str(), kFontTitle, 1.0f).x > titleMaxW - 1.0f)
    {
        // Truncate with ellipsis if we shortened.
        if (title.size() > 3)
            title = title.substr(0, title.size() - 3) + "...";
    }

    DrawTextEx(
        headerFont,
        title.c_str(),
        {dialog.x + 18.0f, dialog.y + 14.0f},
        kFontTitle,
        1.0f,
        kTextPrimary);

    const float btnH = 34.0f;
    const float btnW = 110.0f;
    const float btnY = dialog.y + dialogH - btnH - 16.0f;
    const Rectangle field = {
        dialog.x + 18.0f,
        dialog.y + 44.0f,
        dialogW - 36.0f,
        btnY - (dialog.y + 44.0f) - 14.0f};

    DrawRectangleRec(field, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(field, 1.0f, kPanelBorder);

    // Publish field metrics so update() can process click/arrow input.
    fieldRect = field;
    fontSize = kFontBody;
    lineHeight = fontSize + 4.0f;
    pad = 8.0f;

    const float drawFontSize = fontSize;
    const float drawLineHeight = lineHeight;
    const float drawPad = pad;
    const float maxTextW = field.width - drawPad * 2.0f;

    const std::vector<EditorVisualLine>& lines = buildEditorVisualLines(maxTextW, drawFontSize);
    const float contentH = static_cast<float>(lines.size()) * drawLineHeight;
    const float maxScroll = std::max(0.0f, contentH - (field.height - drawPad * 2.0f));
    if (CheckCollisionPointRec(GetMousePosition(), field))
        scrollY -= GetMouseWheelMove() * drawLineHeight;
    if (scrollY < 0.0f)
        scrollY = 0.0f;
    if (scrollY > maxScroll)
        scrollY = maxScroll;

    const bool highlightTts = showTts && textTtsEnabled;
    if (highlightTts)
        rebuildTtsHighlightColors();

    const bool caretOn = (static_cast<int>(GetTime() * 2.0) % 2) == 0;
    clampVariableCursor();
    const int caret = cursor;
    const int caretLine = editorLineIndexForCursor(lines, caret);
    int selStart = 0;
    int selEnd = 0;
    const bool hasSel = variableHasSelection();
    if (hasSel)
        variableSelectionRange(selStart, selEnd);

    BeginScissorMode(
        static_cast<int>(field.x),
        static_cast<int>(field.y),
        static_cast<int>(field.width),
        static_cast<int>(field.height));

    float y = field.y + drawPad - scrollY;
    if (!multiline)
        y = field.y + (field.height - drawFontSize) * 0.5f;

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const EditorVisualLine& line = lines[lineIndex];

        // Selection highlight for this visual line.
        if (hasSel)
        {
            const int lineSelStart = std::max(selStart, line.start);
            const int lineSelEnd = std::min(selEnd, line.end);
            if (lineSelStart < lineSelEnd)
            {
                const float x0 = field.x + pad + editorCaretXOnLine(line, lineSelStart, drawFontSize);
                const float x1 = field.x + pad + editorCaretXOnLine(line, lineSelEnd, drawFontSize);
                DrawRectangleRec(
                    {x0, y, std::max(2.0f, x1 - x0), drawFontSize + 2.0f},
                    Color{70, 90, 140, 180});
            }
        }

        drawEditorLineText(line, field.x + drawPad, y, drawFontSize, highlightTts);

        if (caretOn && !hasSel && static_cast<int>(lineIndex) == caretLine)
        {
            const float caretX =
                field.x + pad + editorCaretXOnLine(line, caret, drawFontSize);
            DrawLineEx({caretX, y}, {caretX, y + drawFontSize}, 1.5f, kTextPrimary);
        }
        else if (caretOn && hasSel && static_cast<int>(lineIndex) == caretLine)
        {
            // Still show caret at active end of the selection.
            const float caretX =
                field.x + pad + editorCaretXOnLine(line, caret, drawFontSize);
            DrawLineEx({caretX, y}, {caretX, y + drawFontSize}, 1.5f, kTextPrimary);
        }

        y += drawLineHeight;
        if (!multiline)
            break;
    }

    EndScissorMode();

    const Rectangle localSaveBtn = {dialog.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
    const Rectangle localCancelBtn = {dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};

    auto drawButton = [&](Rectangle bounds, const char* label, bool accent)
    {
        DrawRectangleRec(bounds, accent ? kPanelAccent : Color{44, 42, 52, 255});
        DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);
        const Vector2 size = MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), label, kFontBody, 1.0f);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            label,
            {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 9.0f},
            kFontBody,
            1.0f,
            kTextPrimary);
    };

    // Keep rects identical to update() hit-testing.
    saveBtn = localSaveBtn;
    cancelBtn = localCancelBtn;

    // Lower-left text/TTS switch (conversation dialog editors only).
    if (textTtsEnabled)
    {
        const float toggleW = 56.0f;
        const float toggleH = 22.0f;
        const float trackX = dialog.x + 18.0f; // align with dialog content left edge
        const Rectangle track = {
            trackX,
            btnY + (btnH - toggleH) * 0.5f,
            toggleW,
            toggleH};
        textTtsToggle = {
            track.x,
            btnY,
            track.width,
            btnH};

        // Track: left half = text, right half = TTS (knob position matches).
        DrawRectangleRounded(track, 0.5f, 6, Color{44, 42, 52, 255});
        DrawRectangleLinesEx(track, 1.0f, kPanelBorder);
        if (showTts)
        {
            // Highlight right half when TTS is active.
            DrawRectangleRec(
                {track.x + track.width * 0.5f, track.y + 1.0f,
                 track.width * 0.5f - 1.0f, track.height - 2.0f},
                kPanelAccent);
        }
        else
        {
            // Highlight left half when text is active.
            DrawRectangleRec(
                {track.x + 1.0f, track.y + 1.0f,
                 track.width * 0.5f - 1.0f, track.height - 2.0f},
                kPanelAccent);
        }

        const float knobSize = toggleH - 6.0f;
        // Left = text, right = TTS
        const float knobX = showTts
            ? (track.x + track.width - knobSize - 3.0f)
            : (track.x + 3.0f);
        DrawRectangleRounded(
            {knobX, track.y + 3.0f, knobSize, knobSize},
            0.5f,
            6,
            kTextPrimary);

        const char* sideLabel = showTts ? "TTS" : "text";
        const Vector2 sideSize = MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), sideLabel, kFontTiny, 1.0f);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            sideLabel,
            {track.x + track.width + 8.0f, track.y + (track.height - sideSize.y) * 0.5f},
            kFontTiny,
            1.0f,
            kPanelBorder);
    }
    else
    {
        textTtsToggle = {0, 0, 0, 0};
    }

    drawButton(localSaveBtn, "Save", true);
    drawButton(localCancelBtn, "Cancel", false);

    if (!error.empty())
    {
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            error.c_str(),
            {dialog.x + 18.0f, btnY - 22.0f},
            kFontTiny,
            1.0f,
            Color{220, 120, 100, 255});
    }

    // Voice menu last so it paints above the text field (was covering options before).
    if (showOwnerTtsDropdown && voiceDropdownOpen && voiceDropdownMenu.height > 1.0f)
    {
        const std::vector<std::string>& voices = builtinVoiceIds();
        const int optionCount = 1 + static_cast<int>(voices.size());
        const float rowH = voiceDropdownMenu.height / static_cast<float>(optionCount);
        DrawRectangleRec(voiceDropdownMenu, Color{28, 26, 34, 255});
        DrawRectangleLinesEx(voiceDropdownMenu, 1.0f, kPanelBorder);
        for (int i = 0; i < optionCount; ++i)
        {
            const std::string opt = (i == 0) ? "OFF" : voices[static_cast<size_t>(i - 1)];
            const Rectangle row = {
                voiceDropdownMenu.x,
                voiceDropdownMenu.y + rowH * static_cast<float>(i),
                voiceDropdownMenu.width,
                rowH};
            const bool selected = (opt == ownerTtsPolicyDropdownLabel());
            if (CheckCollisionPointRec(GetMousePosition(), row) || selected)
                DrawRectangleRec(row, selected ? Color{70, 60, 90, 255} : Color{60, 55, 75, 255});
            DrawTextEx(
                headerFont,
                opt.c_str(),
                {row.x + 8.0f, row.y + 4.0f},
                kFontBody,
                1.0f,
                kTextPrimary);
        }
    }

    // Enter saves single-line fields; multiline uses Enter for newlines.
    if (ignoreInputFrames <= 0 &&
        !multiline &&
        (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))
    {
        if (!saveVariableEditor() && error.empty())
            error = "Could not parse value — check type and try again";
    }
}


void VariableEditor::drawVariablesPane(Rectangle paneBounds)
{
    // Capture once so list + editor always use the same scene for this frame.
    const std::string sceneId = (*selectionSceneId);

    const Rectangle editBtn = {
        paneBounds.x + paneBounds.width - 72.0f,
        paneBounds.y + 6.0f,
        60.0f,
        20.0f};

    const char* title = "Scene Variables";
    const float titleX = paneBounds.x + 12.0f;
    const float titleY = paneBounds.y + 8.0f;
    DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), title, {titleX, titleY}, kFontLabel, 1.0f, kTextMuted);

    if (!sceneId.empty())
    {
        const float titleW = measureUiTextWidth(title, kFontLabel);
        const float sceneX = titleX + titleW + 14.0f;
        const float sceneMaxW = std::max(40.0f, editBtn.x - 12.0f - sceneX);
        std::string sceneLabel = sceneId;
        if (measureUiTextWidth(sceneLabel, kFontTiny) > sceneMaxW)
        {
            while (!sceneLabel.empty() &&
                   measureUiTextWidth(sceneLabel + "...", kFontTiny) > sceneMaxW)
            {
                sceneLabel.pop_back();
            }
            sceneLabel += "...";
        }
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            sceneLabel.c_str(),
            {sceneX, paneBounds.y + 10.0f},
            kFontTiny,
            1.0f,
            kPanelBorder);
    }

    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        "Click a row to edit",
        {paneBounds.x + 12.0f, paneBounds.y + paneBounds.height - 18.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    DrawRectangleRec(editBtn, kPanelAccent);
    DrawRectangleLinesEx(editBtn, 1.0f, kPanelBorder);
    DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "Edit", {editBtn.x + 16.0f, editBtn.y + 3.0f}, kFontTiny, 1.0f, kTextPrimary);

    if (sceneId.empty() || !docs->scenes.hasScene(sceneId))
    {
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                   kFontBody, 1.0f, kTextMuted);
        return;
    }

    const std::vector<std::pair<std::string, std::string>> rows =
        docs->scenes.sceneVariableRows(sceneId);
    if (rows.empty())
    {
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "No variables on this scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                   kFontBody, 1.0f, kTextMuted);
        return;
    }

    if (selectedVariableKey.empty() ||
        std::find_if(rows.begin(), rows.end(), [&](const std::pair<std::string, std::string>& row)
                     { return row.first == selectedVariableKey; }) == rows.end())
    {
        selectedVariableKey = rows.front().first;
    }

    const float rowHeight = 24.0f;
    const float listTop = paneBounds.y + 28.0f;
    const float listHeight = paneBounds.height - 36.0f;
    const float contentHeight = static_cast<float>(rows.size()) * rowHeight + 8.0f;
    const float maxScroll = std::max(0.0f, contentHeight - listHeight);
    if ((*variablesScroll) > maxScroll)
        (*variablesScroll) = maxScroll;

    const Rectangle listBounds = {paneBounds.x, listTop, paneBounds.width, listHeight};
    const Vector2 mouse = GetMousePosition();
    const bool canInteract = !open && !(*stackDialogOpen);

    if (canInteract && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, editBtn) && !selectedVariableKey.empty())
        {
            openVariableEditor(sceneId, selectedVariableKey);
        }
        else if (CheckCollisionPointRec(mouse, listBounds))
        {
            const float localY = (mouse.y - listTop - 8.0f) + (*variablesScroll);
            if (localY >= 0.0f)
            {
                const int index = static_cast<int>(localY / rowHeight);
                if (index >= 0 && index < static_cast<int>(rows.size()))
                {
                    selectedVariableKey = rows[static_cast<size_t>(index)].first;
                    openVariableEditor(sceneId, selectedVariableKey);
                }
            }
        }
    }

    if (canInteract &&
        !selectedVariableKey.empty() &&
        (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_F2)))
    {
        openVariableEditor(sceneId, selectedVariableKey);
    }

    BeginScissorMode(
        static_cast<int>(listBounds.x),
        static_cast<int>(listBounds.y),
        static_cast<int>(listBounds.width),
        static_cast<int>(listBounds.height));

    float y = listTop + 8.0f - (*variablesScroll);
    for (const std::pair<std::string, std::string>& row : rows)
    {
        const Rectangle rowBounds = {
            paneBounds.x + 8.0f,
            y,
            paneBounds.width - 16.0f,
            rowHeight - 2.0f};
        const bool hovered =
            canInteract &&
            CheckCollisionPointRec(mouse, listBounds) &&
            CheckCollisionPointRec(mouse, rowBounds);
        const bool selected = row.first == selectedVariableKey;

        if (selected)
            DrawRectangleRec(rowBounds, kSelection);
        else if (hovered)
            DrawRectangleRec(rowBounds, Color{60, 54, 72, 180});

        const std::string line = row.first + ": " + truncate(row.second, 80);
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), line.c_str(), {rowBounds.x + 4.0f, rowBounds.y + 4.0f},
                   kFontSmall, 1.0f, kTextPrimary);

        y += rowHeight;
    }

    EndScissorMode();

    if (canInteract && CheckCollisionPointRec(mouse, listBounds))
        (*variablesScroll) -= GetMouseWheelMove() * 18.0f;
    if ((*variablesScroll) < 0.0f)
        (*variablesScroll) = 0.0f;
    if ((*variablesScroll) > maxScroll)
        (*variablesScroll) = maxScroll;
}


bool VariableEditor::isTtsJsonKey(const std::string& key)
{
    if (key == "tts" || key == "ttsVoice" || key == "ttsText" || key == "ttsAudio" ||
        key == "ttsAudioSegments" || key == "ttsTextSha256" ||
        key == "ttsAfter" || key == "ttsAfterVoice" || key == "ttsAfterText" ||
        key == "ttsAfterAudio" || key == "ttsAfterAudioSegments" ||
        key == "resumeTts" || key == "resumeTtsVoice" || key == "resumeTtsText" ||
        key == "resumeTtsAudio")
        return true;
    // Nested TTS bags on scene narrative fields
    if (key.size() >= 3 && key.compare(key.size() - 3, 3, "Tts") == 0)
        return true;
    return false;
}


nlohmann::json VariableEditor::stripTtsKeys(const nlohmann::json& object)
{
    nlohmann::json out = nlohmann::json::object();
    if (!object.is_object())
        return out;
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!isTtsJsonKey(it.key()))
            out[it.key()] = it.value();
    }
    return out;
}


nlohmann::json VariableEditor::onlyTtsKeys(const nlohmann::json& object)
{
    nlohmann::json out = nlohmann::json::object();
    if (!object.is_object())
        return out;
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (isTtsJsonKey(it.key()))
            out[it.key()] = it.value();
    }
    return out;
}


std::string VariableEditor::narrativeTtsObjectKey(const std::string& fieldLeaf) const
{
    if (fieldLeaf == "description")
    {
        // Items store examine speech in examineTts; scenes use descriptionTts.
        return docTarget == ConversationEditDoc::Items ? "examineTts" : "descriptionTts";
    }
    if (fieldLeaf == "examineDetails")
        return "examineTts";
    if (fieldLeaf == "speakDetails")
        return "speakTts";
    if (fieldLeaf == "useDetails" || fieldLeaf == "useNarrative")
        return "useTts";
    if (fieldLeaf == "wakeNarrative")
        return "wakeTts";
    return "";
}


void VariableEditor::ensureGlobalDefaultVoiceLoaded()
{
    if (globalDefaultVoiceLoaded)
        return;
    globalDefaultVoiceLoaded = true;
    globalDefaultVoice = "leo";

    const std::string configPath = pathJoin(docs->resourceDir, "game_config.json");
    std::ifstream file(configPath.c_str());
    if (!file.is_open())
        return;

    try
    {
        nlohmann::json config;
        file >> config;
        if (config.is_object() && config.contains("tts") && config["tts"].is_object())
        {
            const std::string voice = config["tts"].value("voice", globalDefaultVoice);
            if (!voice.empty())
                globalDefaultVoice = voice;
        }
    }
    catch (const nlohmann::json::exception&)
    {
    }
}


void VariableEditor::ensureTtsSyntaxThemeLoaded()
{
    if (ttsSyntaxThemeLoaded)
        return;
    ttsSyntaxThemeLoaded = true;

    // Built-in defaults match editor_tts_theme.json.
    ttsSyntaxTheme = TtsSyntaxTheme{};

    const std::string themePath = pathJoin(docs->resourceDir, "editor_tts_theme.json");
    std::ifstream file(themePath.c_str());
    if (!file.is_open())
    {
        TraceLog(LOG_INFO, "TIMBERLINE: TTS theme not found (%s); using defaults", themePath.c_str());
        return;
    }

    try
    {
        nlohmann::json root;
        file >> root;
        const nlohmann::json& syntax = root.contains("ttsSyntax") && root["ttsSyntax"].is_object()
            ? root["ttsSyntax"]
            : root;
        if (!syntax.is_object())
            return;

        if (syntax.contains("default"))
            ttsSyntaxTheme.defaultColor = colorFromJsonRgba(syntax["default"], ttsSyntaxTheme.defaultColor);
        if (syntax.contains("command"))
            ttsSyntaxTheme.command = colorFromJsonRgba(syntax["command"], ttsSyntaxTheme.command);
        if (syntax.contains("voiceMarkup"))
            ttsSyntaxTheme.voiceMarkup =
                colorFromJsonRgba(syntax["voiceMarkup"], ttsSyntaxTheme.voiceMarkup);
        if (syntax.contains("voiceDialog"))
            ttsSyntaxTheme.voiceDialog =
                colorFromJsonRgba(syntax["voiceDialog"], ttsSyntaxTheme.voiceDialog);
        if (syntax.contains("voiceDialogError"))
            ttsSyntaxTheme.voiceDialogError =
                colorFromJsonRgba(syntax["voiceDialogError"], ttsSyntaxTheme.voiceDialogError);

        TraceLog(LOG_INFO, "TIMBERLINE: loaded TTS syntax theme %s", themePath.c_str());
    }
    catch (const nlohmann::json::exception& ex)
    {
        TraceLog(LOG_WARNING, "TIMBERLINE: failed to parse TTS theme: %s", ex.what());
    }
}


bool VariableEditor::isTtsCommandBodyChar(unsigned char ch)
{
    return std::isalnum(ch) || ch == '-' || ch == '_' || ch == ' ' || ch == '.';
}


bool VariableEditor::looksLikeTtsCommandBody(const std::string& body)
{
    // Keep in sync with timberline_engine allowlist used by classifyTtsTextHighlight.
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


bool VariableEditor::isVoiceOpenTagBody(const std::string& body)
{
    std::string trimmed = body;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    if (trimmed.empty() || trimmed[0] == '/')
        return false;

    std::string lower = normalizeVoiceId(trimmed);
    if (lower.rfind("voice:", 0) == 0)
        return lower.size() > 6;
    return isKnownBuiltinVoiceId(lower);
}


bool VariableEditor::isVoiceCloseTagBody(const std::string& body)
{
    std::string trimmed = body;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    if (trimmed.empty() || trimmed[0] != '/')
        return false;

    std::string name = normalizeVoiceId(trimmed.substr(1));
    return name == "voice" || isKnownBuiltinVoiceId(name);
}


void VariableEditor::rebuildTtsHighlightColors()
{
    ensureTtsSyntaxThemeLoaded();
    const std::string& text = buffer;
    if (ttsHighlightCacheSource == text &&
        ttsHighlightColors.size() == text.size())
        return;

    ttsHighlightCacheSource = text;
    ttsHighlightColors.assign(text.size(), ttsSyntaxTheme.defaultColor);
    if (text.empty())
        return;

    std::vector<TtsHighlightKind> kinds;
    classifyTtsTextHighlight(text, kinds);
    for (size_t i = 0; i < kinds.size(); ++i)
    {
        switch (kinds[i])
        {
            case TtsHighlightKind::Command:
                ttsHighlightColors[i] = ttsSyntaxTheme.command;
                break;
            case TtsHighlightKind::VoiceMarkup:
                ttsHighlightColors[i] = ttsSyntaxTheme.voiceMarkup;
                break;
            case TtsHighlightKind::VoiceDialog:
                ttsHighlightColors[i] = ttsSyntaxTheme.voiceDialog;
                break;
            case TtsHighlightKind::VoiceDialogError:
                ttsHighlightColors[i] = ttsSyntaxTheme.voiceDialogError;
                break;
            case TtsHighlightKind::Default:
            default:
                ttsHighlightColors[i] = ttsSyntaxTheme.defaultColor;
                break;
        }
    }
}


Color VariableEditor::ttsColorAtBufferIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(ttsHighlightColors.size()))
        return ttsSyntaxTheme.defaultColor;
    return ttsHighlightColors[static_cast<size_t>(index)];
}


bool VariableEditor::colorsEqual(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}


Color VariableEditor::colorFromJsonRgba(const nlohmann::json& node, Color fallback)
{
    if (!node.is_array() || node.size() < 3)
        return fallback;
    Color c = fallback;
    try
    {
        c.r = static_cast<unsigned char>(std::max(0, std::min(255, node[0].get<int>())));
        c.g = static_cast<unsigned char>(std::max(0, std::min(255, node[1].get<int>())));
        c.b = static_cast<unsigned char>(std::max(0, std::min(255, node[2].get<int>())));
        if (node.size() >= 4)
            c.a = static_cast<unsigned char>(std::max(0, std::min(255, node[3].get<int>())));
        else
            c.a = 255;
    }
    catch (...)
    {
        return fallback;
    }
    return c;
}


std::string VariableEditor::readTtsVoiceFromObject(const nlohmann::json& object) const
{
    if (!object.is_object())
        return "";
    if (object.contains("ttsVoice") && object["ttsVoice"].is_string())
    {
        const std::string voice = object["ttsVoice"].get<std::string>();
        if (!voice.empty())
            return voice;
    }
    return "";
}


std::string VariableEditor::ownerTtsPolicyDropdownLabel() const
{
    if (docs == nullptr)
        return "OFF";

    const nlohmann::json* owner = nullptr;
    if (docTarget == ConversationEditDoc::Items)
        owner = docs->itemJson(editorItemId);
    else if (!editorSceneId.empty())
        owner = docs->scenes.sceneJson(editorSceneId);

    if (owner == nullptr || !owner->is_object())
        return "OFF";
    if (!owner->value("ttsEnabled", false))
        return "OFF";
    const std::string voice = normalizeVoiceId(owner->value("ttsDefaultVoice", ""));
    if (voice.empty() || !isKnownBuiltinVoiceId(voice))
        return "OFF";
    return voice;
}

bool VariableEditor::applyOwnerTtsPolicySelection(const std::string& selection)
{
    if (docs == nullptr)
        return false;

    nlohmann::json* owner = nullptr;
    const bool isItem = docTarget == ConversationEditDoc::Items;
    if (isItem)
        owner = docs->itemJson(editorItemId);
    else if (!editorSceneId.empty())
        owner = docs->scenes.sceneJson(editorSceneId);

    if (owner == nullptr || !owner->is_object())
        return false;

    if (selection == "OFF" || selection.empty())
    {
        (*owner)["ttsEnabled"] = false;
        owner->erase("ttsDefaultVoice");
    }
    else
    {
        const std::string voice = normalizeVoiceId(selection);
        if (!isKnownBuiltinVoiceId(voice))
        {
            error = "Unknown voice \"" + selection + "\"";
            return false;
        }
        (*owner)["ttsEnabled"] = true;
        (*owner)["ttsDefaultVoice"] = voice;
    }

    docs->markDirty();
    if (isItem)
    {
        if (!docs->saveItemsDocument())
        {
            error = "Updated TTS policy in memory, but failed to write items.json";
            return false;
        }
    }
    else if (!docs->scenes.save())
    {
        error = "Updated TTS policy in memory, but failed to write scenes.json";
        return false;
    }
    docs->dirty = false;
    if (onTreeRebuild)
        onTreeRebuild();
    return true;
}

std::string VariableEditor::editorDefaultVoiceLabel() const
{
    // Prefer the voice on the object being edited, then parent / TTS bag, then game config.
    if (textTtsEnabled && textTtsMode == TextTtsPairMode::ObjectSplit)
    {
        try
        {
            const nlohmann::json ttsPart = showTts
                ? (buffer.empty()
                       ? nlohmann::json::object()
                       : nlohmann::json::parse(buffer))
                : (ttsSideBuffer.empty()
                       ? nlohmann::json::object()
                       : nlohmann::json::parse(ttsSideBuffer));
            const std::string fromTts = readTtsVoiceFromObject(ttsPart);
            if (!fromTts.empty())
                return fromTts;
        }
        catch (const nlohmann::json::exception&)
        {
        }
    }

    if (docTarget == ConversationEditDoc::Conversations ||
        docTarget == ConversationEditDoc::Scenes ||
        docTarget == ConversationEditDoc::Items)
    {
        std::string parentPointer;
        std::string leaf;
        if (VariableEditor::splitJsonPointer(jsonPointer, parentPointer, leaf))
        {
            // Mutable helper used as const via const_cast pattern avoided —
            // resolve parent through document reads only.
            const nlohmann::json* parent = nullptr;
            if (docTarget == ConversationEditDoc::Conversations)
            {
                if (parentPointer.empty())
                    parent = docs->conversationsLoaded ? &docs->conversationsRoot : nullptr;
                else
                    parent = docs->conversationJsonAt(parentPointer);
            }
            else if (docTarget == ConversationEditDoc::Items)
            {
                if (parentPointer.empty())
                    parent = docs->itemJson(editorItemId);
                else
                    parent = docs->itemFieldAt(editorItemId, parentPointer);
            }
            else
            {
                if (parentPointer.empty())
                    parent = docs->scenes.sceneJson(editorSceneId);
                else
                    parent = docs->sceneFieldAt(editorSceneId, parentPointer);
            }

            if (parent != nullptr && parent->is_object())
            {
                if (textTtsMode == TextTtsPairMode::StringWithTtsObject &&
                    !ttsObjectKey.empty() &&
                    parent->contains(ttsObjectKey) &&
                    (*parent)[ttsObjectKey].is_object())
                {
                    const std::string fromBag =
                        readTtsVoiceFromObject((*parent)[ttsObjectKey]);
                    if (!fromBag.empty())
                        return fromBag;
                }

                const std::string fromParent = readTtsVoiceFromObject(*parent);
                if (!fromParent.empty())
                    return fromParent;
            }
        }

        // Direct object at pointer (milestone / choice / item bag).
        const nlohmann::json* direct = nullptr;
        if (docTarget == ConversationEditDoc::Conversations)
            direct = docs->conversationJsonAt(jsonPointer);
        else if (docTarget == ConversationEditDoc::Items)
            direct = docs->itemFieldAt(editorItemId, jsonPointer);
        else
            direct = docs->sceneFieldAt(editorSceneId, jsonPointer);
        if (direct != nullptr && direct->is_object())
        {
            const std::string fromDirect = readTtsVoiceFromObject(*direct);
            if (!fromDirect.empty())
                return fromDirect;
        }
    }

    return globalDefaultVoice;
}


void VariableEditor::toggleTextTtsSide()
{
    if (!textTtsEnabled)
        return;
    stashActiveBufferToSide();
    showTts = !showTts;
    syncActiveBufferFromSide();
}


void VariableEditor::setupTextTtsForOpenedValue(const nlohmann::json& value)
{
    textTtsEnabled = false;
    showTts = false;
    textSideBuffer.clear();
    ttsSideBuffer.clear();
    textTtsMode = TextTtsPairMode::None;
    ttsObjectKey.clear();

    // Conversation, scene, item, and scene-variable dialog editors share Text/TTS.
    if (docTarget != ConversationEditDoc::Conversations &&
        docTarget != ConversationEditDoc::Scenes &&
        docTarget != ConversationEditDoc::Items &&
        docTarget != ConversationEditDoc::None)
        return;
    if (docTarget == ConversationEditDoc::None && editorSceneId.empty())
        return;
    if (docTarget == ConversationEditDoc::Items && editorItemId.empty())
        return;

    if (value.is_object())
    {
        const nlohmann::json nonTts = stripTtsKeys(value);
        const bool pureTtsBag = nonTts.empty()
            && (value.contains("ttsText") || value.contains("tts") || value.contains("ttsAudio")
                || value.contains("enabled") || value.contains("voice") || value.contains("text")
                || value.contains("audio"));

        // Pure TTS bags (wakeTts, descriptionTts, examineTts, …): edit spoken text as a
        // string, not the whole JSON dump. Prefer ttsText, then legacy "text".
        if (pureTtsBag)
        {
            std::string spoken;
            if (value.contains("ttsText") && value["ttsText"].is_string())
                spoken = value["ttsText"].get<std::string>();
            else if (value.contains("text") && value["text"].is_string())
                spoken = value["text"].get<std::string>();

            textTtsMode = TextTtsPairMode::TtsBagSpokenText;
            textSideBuffer = spoken;
            ttsSideBuffer = spoken;
            textTtsEnabled = true;
            showTts = true; // bags are speech-first
            kind = VariableKindString;
            multiline = true;
            syncActiveBufferFromSide();
            return;
        }

        textTtsMode = TextTtsPairMode::ObjectSplit;
        textSideBuffer = nonTts.dump(2);
        ttsSideBuffer = onlyTtsKeys(value).dump(2);
        textTtsEnabled = true;
        showTts = false;
        syncActiveBufferFromSide();
        return;
    }

    if (!value.is_string() && !value.is_null())
        return;

    std::string parentPointer;
    std::string leaf;
    if (!VariableEditor::splitJsonPointer(jsonPointer, parentPointer, leaf))
        return;

    textSideBuffer = value.is_string() ? value.get<std::string>() : "";

    const std::string ttsObjKey = narrativeTtsObjectKey(leaf);
    nlohmann::json* parent = resolveEditorParentObject(parentPointer);

    if (!ttsObjKey.empty() && parent != nullptr && parent->is_object())
    {
        textTtsMode = TextTtsPairMode::StringWithTtsObject;
        ttsObjectKey = ttsObjKey;
        if (parent->contains(ttsObjKey) && (*parent)[ttsObjKey].is_object())
        {
            const nlohmann::json& bag = (*parent)[ttsObjKey];
            if (bag.contains("ttsText") && bag["ttsText"].is_string())
                ttsSideBuffer = bag["ttsText"].get<std::string>();
            else if (bag.contains("text") && bag["text"].is_string())
                ttsSideBuffer = bag["text"].get<std::string>();
        }
        else if (parent->contains("ttsText") && (*parent)["ttsText"].is_string())
        {
            // Fall back to flat ttsText on same object if nested bag missing.
            ttsSideBuffer = (*parent)["ttsText"].get<std::string>();
        }

        // If there is no dedicated TTS line yet, seed from on-screen text so the
        // TTS pane is not blank when speech is intended to match description.
        if (ttsSideBuffer.empty())
            ttsSideBuffer = textSideBuffer;

        textTtsEnabled = true;
        showTts = false;
        syncActiveBufferFromSide();
        return;
    }

    // Sibling ttsText on parent (conversation intro/response/text, etc.)
    if (parent != nullptr && parent->is_object()
        && parent->contains("ttsText") && (*parent)["ttsText"].is_string())
    {
        textTtsMode = TextTtsPairMode::StringWithSiblingTtsText;
        ttsSideBuffer = (*parent)["ttsText"].get<std::string>();
        if (ttsSideBuffer.empty())
            ttsSideBuffer = textSideBuffer;
        textTtsEnabled = true;
        showTts = false;
        syncActiveBufferFromSide();
    }
}


bool VariableEditor::applyTextTtsSidesToDocument()
{
    stashActiveBufferToSide();

    if (textTtsMode == TextTtsPairMode::TtsBagSpokenText)
    {
        nlohmann::json* target = nullptr;
        if (docTarget == ConversationEditDoc::Conversations)
            target = docs->conversationJsonAt(jsonPointer);
        else if (docTarget == ConversationEditDoc::Items)
            target = docs->itemFieldAt(editorItemId, jsonPointer);
        else if (docTarget == ConversationEditDoc::Scenes || docTarget == ConversationEditDoc::None)
            target = docs->sceneFieldAt(editorSceneId, jsonPointer);
        if (target == nullptr || !target->is_object())
            return false;

        // TTS side is authoritative for spoken line; keep other bag fields intact.
        const std::string spoken =
            !ttsSideBuffer.empty() ? ttsSideBuffer : textSideBuffer;
        (*target)["ttsText"] = spoken;
        (*target)["tts"] = !spoken.empty();
        if (!target->contains("ttsVoice") || !(*target)["ttsVoice"].is_string())
            (*target)["ttsVoice"] = "";

        // New item/scene bags need a stable audio path before --refresh-voices will collect them.
        if (spoken.empty())
            return true;
        const std::string existingAudio = target->value("ttsAudio", target->value("audio", ""));
        if (existingAudio.empty())
        {
            std::string leaf;
            std::string parentPointer;
            if (VariableEditor::splitJsonPointer(jsonPointer, parentPointer, leaf))
            {
                if (docTarget == ConversationEditDoc::Items && !editorItemId.empty())
                {
                    std::string suffix = "examine";
                    if (leaf == "useTts")
                        suffix = "use";
                    else if (leaf == "takeTts")
                        suffix = "take";
                    else if (leaf == "examineTts")
                        suffix = "examine";
                    (*target)["ttsAudio"] =
                        "resources/audio/tts/items/" + editorItemId + "/" + suffix + ".mp3";
                }
                else if (!editorSceneId.empty())
                {
                    (*target)["ttsAudio"] =
                        "resources/audio/tts/" + editorSceneId + "/" + leaf + ".mp3";
                }
            }
        }
        return true;
    }

    if (textTtsMode == TextTtsPairMode::ObjectSplit)
    {
        nlohmann::json* target = nullptr;
        if (docTarget == ConversationEditDoc::Conversations)
            target = docs->conversationJsonAt(jsonPointer);
        else if (docTarget == ConversationEditDoc::Items)
            target = docs->itemFieldAt(editorItemId, jsonPointer);
        else if (docTarget == ConversationEditDoc::Scenes || docTarget == ConversationEditDoc::None)
            target = docs->sceneFieldAt(editorSceneId, jsonPointer);
        if (target == nullptr || !target->is_object())
            return false;

        nlohmann::json textPart;
        nlohmann::json ttsPart;
        try
        {
            textPart = textSideBuffer.empty()
                ? nlohmann::json::object()
                : nlohmann::json::parse(textSideBuffer);
            ttsPart = ttsSideBuffer.empty()
                ? nlohmann::json::object()
                : nlohmann::json::parse(ttsSideBuffer);
        }
        catch (const nlohmann::json::exception&)
        {
            return false;
        }
        if (!textPart.is_object() || !ttsPart.is_object())
            return false;

        // Drop previous TTS keys, then merge both sides.
        nlohmann::json merged = stripTtsKeys(*target);
        for (auto it = textPart.begin(); it != textPart.end(); ++it)
        {
            if (!isTtsJsonKey(it.key()))
                merged[it.key()] = it.value();
        }
        // Remove TTS keys not present in the TTS side (allows clearing).
        for (auto it = merged.begin(); it != merged.end();)
        {
            if (isTtsJsonKey(it.key()))
                it = merged.erase(it);
            else
                ++it;
        }
        for (auto it = ttsPart.begin(); it != ttsPart.end(); ++it)
            merged[it.key()] = it.value();

        *target = merged;
        return true;
    }

    if (textTtsMode == TextTtsPairMode::StringWithTtsObject ||
        textTtsMode == TextTtsPairMode::StringWithSiblingTtsText)
    {
        nlohmann::json* textTarget = nullptr;
        if (docTarget == ConversationEditDoc::Conversations)
            textTarget = docs->conversationJsonAt(jsonPointer);
        else if (docTarget == ConversationEditDoc::Items)
            textTarget = docs->itemFieldAt(editorItemId, jsonPointer);
        else if (docTarget == ConversationEditDoc::Scenes || docTarget == ConversationEditDoc::None)
            textTarget = docs->sceneFieldAt(editorSceneId, jsonPointer);
        if (textTarget == nullptr)
            return false;
        *textTarget = textSideBuffer;

        std::string parentPointer;
        std::string leaf;
        if (!VariableEditor::splitJsonPointer(jsonPointer, parentPointer, leaf))
            return false;
        nlohmann::json* parent = resolveEditorParentObject(parentPointer);
        if (parent == nullptr || !parent->is_object())
            return false;

        if (textTtsMode == TextTtsPairMode::StringWithTtsObject &&
            !ttsObjectKey.empty())
        {
            if (!parent->contains(ttsObjectKey) || !(*parent)[ttsObjectKey].is_object())
                (*parent)[ttsObjectKey] = nlohmann::json::object();
            nlohmann::json& bag = (*parent)[ttsObjectKey];
            bag["ttsText"] = ttsSideBuffer;
            bag["tts"] = !ttsSideBuffer.empty();
            if (!bag.contains("ttsVoice") || !bag["ttsVoice"].is_string())
                bag["ttsVoice"] = "";
            if (!ttsSideBuffer.empty())
            {
                const std::string existingAudio = bag.value("ttsAudio", bag.value("audio", ""));
                if (existingAudio.empty())
                {
                    if (docTarget == ConversationEditDoc::Items && !editorItemId.empty())
                    {
                        std::string suffix = "examine";
                        if (ttsObjectKey == "useTts")
                            suffix = "use";
                        else if (ttsObjectKey == "takeTts")
                            suffix = "take";
                        bag["ttsAudio"] =
                            "resources/audio/tts/items/" + editorItemId + "/" + suffix + ".mp3";
                    }
                    else if (!editorSceneId.empty())
                    {
                        bag["ttsAudio"] =
                            "resources/audio/tts/" + editorSceneId + "/" + ttsObjectKey + ".mp3";
                    }
                }
            }
        }
        else
        {
            (*parent)["ttsText"] = ttsSideBuffer;
            (*parent)["tts"] = !ttsSideBuffer.empty();
        }
        return true;
    }

    return false;
}


std::string VariableEditor::unescapeJsonPointerToken(const std::string& token)
{
    std::string out;
    out.reserve(token.size());
    for (size_t i = 0; i < token.size(); ++i)
    {
        if (token[i] == '~' && i + 1 < token.size())
        {
            if (token[i + 1] == '0')
            {
                out.push_back('~');
                ++i;
                continue;
            }
            if (token[i + 1] == '1')
            {
                out.push_back('/');
                ++i;
                continue;
            }
        }
        out.push_back(token[i]);
    }
    return out;
}


float VariableEditor::measureUiTextWidth(const std::string& text, float fontSize) const
{
    if (text.empty())
        return 0.0f;

    if (fontSize != measureCacheFontSize)
    {
        measureCacheFontSize = fontSize;
        measureCacheAsciiReady = false;
        measureCacheStrings.clear();
    }

    // Short strings (labels, glyphs) get a full-string cache.
    if (text.size() <= 96)
    {
        std::map<std::string, float>::const_iterator it = measureCacheStrings.find(text);
        if (it != measureCacheStrings.end())
            return it->second;
        const float w = MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), text.c_str(), fontSize, 1.0f).x;
        if (measureCacheStrings.size() < 4096)
            measureCacheStrings[text] = w;
        return w;
    }

    return MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), text.c_str(), fontSize, 1.0f).x;
}


float VariableEditor::measureUiCharWidth(unsigned char ch, float fontSize) const
{
    if (fontSize != measureCacheFontSize)
    {
        measureCacheFontSize = fontSize;
        measureCacheAsciiReady = false;
        measureCacheStrings.clear();
    }

    if (ch < 128)
    {
        if (!measureCacheAsciiReady)
        {
            char sample[2] = {0, 0};
            for (int i = 0; i < 128; ++i)
            {
                sample[0] = static_cast<char>(i);
                if (i < 32)
                    measureCacheAscii[i] = 0.0f;
                else
                    measureCacheAscii[i] =
                        MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), sample, fontSize, 1.0f).x;
            }
            measureCacheAsciiReady = true;
        }
        return measureCacheAscii[ch];
    }

    char sample[5] = {0, 0, 0, 0, 0};
    sample[0] = static_cast<char>(ch);
    return MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), sample, fontSize, 1.0f).x;
}


std::string VariableEditor::truncate(const std::string& text, size_t maxLen) const
{
    return truncateForTree(text, maxLen);
}

std::string VariableEditor::truncateForTree(const std::string& text, size_t maxLen)
{
    std::string compact;
    compact.reserve(text.size());
    bool lastSpace = false;
    for (char ch : text)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            if (!lastSpace && !compact.empty())
            {
                compact.push_back(' ');
                lastSpace = true;
            }
            continue;
        }
        compact.push_back(ch);
        lastSpace = (ch == ' ');
    }
    if (compact.size() <= maxLen)
        return compact;
    if (maxLen == 0)
        return "";
    if (maxLen == 1)
        return "…";
    return compact.substr(0, maxLen - 1) + "…";
}

} // namespace timberline_editor
