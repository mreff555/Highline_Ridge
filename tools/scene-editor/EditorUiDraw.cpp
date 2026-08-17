/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Shared editor text layout / draw helpers.
 ******************************************************************************/

#include "EditorUiDraw.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

using timberline_engine::classifyTtsTextHighlight;
using timberline_engine::TtsHighlightKind;

namespace timberline_editor
{

float measureUiTextWidth(Font font, const std::string& text, float fontSize)
{
    if (text.empty())
        return 0.0f;
    return MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x;
}

void drawWrappedText(
    Font font,
    const std::string& text,
    Vector2 position,
    float maxWidth,
    float fontSize,
    float lineSpacing,
    Color color)
{
    // Keep legacy simple drawer for labels; caret-sensitive editors use layoutWrappedTextLines.
    std::string line;
    float y = position.y;

    auto flushLine = [&]()
    {
        if (line.empty())
            return;
        DrawTextEx(font, line.c_str(), {position.x, y}, fontSize, 1.0f, color);
        y += fontSize + lineSpacing;
        line.clear();
    };

    std::istringstream stream(text);
    std::string word;
    while (stream >> word)
    {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 1.0f).x <= maxWidth)
        {
            line = candidate;
            continue;
        }

        flushLine();
        line = word;
    }

    flushLine();
}

std::vector<EditorVisualLine> layoutWrappedTextLines(
    Font font,
    const std::string& buffer,
    float maxTextWidth,
    float fontSize)
{
    std::vector<EditorVisualLine> lines;
    const float wrapWidth = std::max(8.0f, maxTextWidth - 2.0f);
    const int n = static_cast<int>(buffer.size());

    auto utf8Next = [&](int at) -> int
    {
        if (at >= n)
            return n;
        int next = at + 1;
        while (next < n
               && (static_cast<unsigned char>(buffer[static_cast<size_t>(next)]) & 0xC0) == 0x80)
            ++next;
        return next;
    };

    auto isSoftBreakByte = [](unsigned char ch) -> bool
    {
        return ch == ' ' || ch == '\t' || ch == '-';
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
        lines.push_back(std::move(line));
    };

    if (n == 0)
    {
        pushLine(0, 0);
        return lines;
    }

    int i = 0;
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
        int lastSoftBreakEnd = -1;

        while (i < n && buffer[static_cast<size_t>(i)] != '\n')
        {
            const int next = utf8Next(i);
            const std::string candidate = buffer.substr(
                static_cast<size_t>(lineStart),
                static_cast<size_t>(next - lineStart));
            const float width = measureUiTextWidth(font, candidate, fontSize);

            if (width > wrapWidth && next > lineStart)
            {
                int wrapEnd = lastSoftBreakEnd;
                if (wrapEnd <= lineStart)
                {
                    if (i == lineStart)
                        wrapEnd = next;
                    else
                        wrapEnd = i;
                }

                pushLine(lineStart, wrapEnd);
                i = wrapEnd;
                if (i < n && buffer[static_cast<size_t>(i)] == ' ')
                    ++i;
                break;
            }

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

    if (!buffer.empty() && buffer.back() == '\n')
        pushLine(n, n);

    if (lines.empty())
        pushLine(0, 0);

    return lines;
}

int visualLineIndexForCursor(
    const std::vector<EditorVisualLine>& lines,
    int cursor,
    int bufferSize)
{
    if (lines.empty())
        return 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const int nextStart = (i + 1 < lines.size())
            ? lines[i + 1].start
            : (bufferSize + 1);
        if (cursor >= lines[i].start && cursor < nextStart)
            return static_cast<int>(i);
    }
    return static_cast<int>(lines.size()) - 1;
}

float caretXOnVisualLine(
    Font font,
    const EditorVisualLine& line,
    int cursor,
    float fontSize)
{
    const int local = std::max(0, std::min(cursor, line.end) - line.start);
    if (local <= 0 || line.text.empty())
        return 0.0f;
    const int take = std::min(local, static_cast<int>(line.text.size()));
    return measureUiTextWidth(font, line.text.substr(0, static_cast<size_t>(take)), fontSize);
}

int cursorIndexFromClick(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    const std::string& buffer,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    float scrollY,
    Vector2 mouse)
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
        if (pos > line.start && pos < line.end && pos < static_cast<int>(buffer.size())
            && (static_cast<unsigned char>(buffer[static_cast<size_t>(pos)]) & 0xC0) == 0x80)
            continue;
        const float x = caretXOnVisualLine(font, line, pos, fontSize);
        const float dist = std::fabs(x - relX);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = pos;
        }
    }
    return best;
}

void drawVisualTextLines(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    float scrollY,
    Color color)
{
    BeginScissorMode(
        static_cast<int>(field.x),
        static_cast<int>(field.y),
        static_cast<int>(field.width),
        static_cast<int>(field.height));

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const float y = field.y + pad + static_cast<float>(i) * lineHeight - scrollY;
        if (y + lineHeight < field.y || y > field.y + field.height)
            continue;
        if (lines[i].text.empty())
            continue;
        DrawTextEx(
            font,
            lines[i].text.c_str(),
            {field.x + pad, y},
            fontSize,
            1.0f,
            color);
    }

    EndScissorMode();
}

Color ttsHighlightKindColor(TtsHighlightKind kind)
{
    switch (kind)
    {
    case TtsHighlightKind::Command:
        return Color{120, 200, 255, 255}; // cyan-ish commands
    case TtsHighlightKind::VoiceMarkup:
        return Color{220, 170, 90, 255};
    case TtsHighlightKind::VoiceDialog:
        return Color{190, 210, 160, 255};
    case TtsHighlightKind::VoiceDialogError:
        return Color{230, 100, 100, 255};
    case TtsHighlightKind::Default:
    default:
        return kTextPrimary;
    }
}

void buildTtsHighlightColors(const std::string& text, std::vector<Color>& outColors)
{
    outColors.assign(text.size(), kTextPrimary);
    if (text.empty())
        return;
    std::vector<TtsHighlightKind> kinds;
    classifyTtsTextHighlight(text, kinds);
    const size_t n = std::min(kinds.size(), outColors.size());
    for (size_t i = 0; i < n; ++i)
        outColors[i] = ttsHighlightKindColor(kinds[i]);
}

void drawVisualTextLinesColored(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    const std::vector<Color>& colors,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    float scrollY,
    Color fallback)
{
    BeginScissorMode(
        static_cast<int>(field.x),
        static_cast<int>(field.y),
        static_cast<int>(field.width),
        static_cast<int>(field.height));

    for (size_t li = 0; li < lines.size(); ++li)
    {
        const EditorVisualLine& line = lines[li];
        const float y = field.y + pad + static_cast<float>(li) * lineHeight - scrollY;
        if (y + lineHeight < field.y || y > field.y + field.height)
            continue;
        if (line.text.empty())
            continue;

        float drawX = field.x + pad;
        size_t i = 0;
        while (i < line.text.size())
        {
            const int bufIdx = line.start + static_cast<int>(i);
            const Color runColor =
                (bufIdx >= 0 && bufIdx < static_cast<int>(colors.size()))
                ? colors[static_cast<size_t>(bufIdx)]
                : fallback;
            size_t j = i + 1;
            while (j < line.text.size())
            {
                const int jIdx = line.start + static_cast<int>(j);
                const Color c =
                    (jIdx >= 0 && jIdx < static_cast<int>(colors.size()))
                    ? colors[static_cast<size_t>(jIdx)]
                    : fallback;
                if (c.r != runColor.r || c.g != runColor.g || c.b != runColor.b
                    || c.a != runColor.a)
                    break;
                ++j;
            }
            const std::string run = line.text.substr(i, j - i);
            DrawTextEx(font, run.c_str(), {drawX, y}, fontSize, 1.0f, runColor);
            drawX += measureUiTextWidth(font, run, fontSize);
            i = j;
        }
    }

    EndScissorMode();
}

int moveCursorVertical(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    const std::string& buffer,
    int cursor,
    int direction,
    float fontSize,
    float& preferredX)
{
    if (lines.empty())
        return cursor;
    const int bufSize = static_cast<int>(buffer.size());
    cursor = std::clamp(cursor, 0, bufSize);
    const int lineIndex = visualLineIndexForCursor(lines, cursor, bufSize);
    const EditorVisualLine& curLine = lines[static_cast<size_t>(lineIndex)];

    if (preferredX < 0.0f)
        preferredX = caretXOnVisualLine(font, curLine, cursor, fontSize);

    int targetLine = lineIndex + direction;
    if (targetLine < 0)
        return lines.front().start;
    if (targetLine >= static_cast<int>(lines.size()))
        return bufSize;

    const EditorVisualLine& line = lines[static_cast<size_t>(targetLine)];
    int best = line.start;
    float bestDist = 1.0e9f;
    for (int pos = line.start; pos <= line.end; ++pos)
    {
        if (pos > line.start && pos < line.end && pos < bufSize
            && (static_cast<unsigned char>(buffer[static_cast<size_t>(pos)]) & 0xC0) == 0x80)
            continue;
        const float x = caretXOnVisualLine(font, line, pos, fontSize);
        const float dist = std::fabs(x - preferredX);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = pos;
        }
    }
    return best;
}

} // namespace timberline_editor
