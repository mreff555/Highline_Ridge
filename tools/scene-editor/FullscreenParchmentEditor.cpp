/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "FullscreenParchmentEditor.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cmath>
#include <vector>

using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

// ~18pt script; Caveat reads small so we size up for legibility on parchment.
constexpr float kScriptFontSize = 26.0f;
constexpr float kLineGap = 8.0f;
constexpr float kParchmentMarginX = 40.0f;
constexpr float kParchmentMarginY = 48.0f;

void insertUtf8(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    char bytes[5] = {};
    int size = 0;
    if (codepoint < 0x80)
    {
        bytes[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    buffer.append(bytes, bytes + size);
}

void backspaceUtf8(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

bool loadTextureFromRoots(
    const std::string& relPath,
    const std::string& resourceDir,
    const std::string& assetRoot,
    Texture2D& out)
{
    std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relPath);
    if (!resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        paths.push_back(pathJoin(resourceDir, stripped));
        paths.push_back(pathJoin(resourceDir, relPath));
    }
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str())
            && loadTextureFromAssetFile(compressed, out))
            return true;
        if (FileExists(path.c_str()) && loadTextureFromAssetFile(path, out))
            return true;
    }
    return false;
}

Font tryLoadScriptFont(const std::string& resourceDir, const std::string& assetRoot)
{
    const char* leaves[] = {
        "resources/fonts/Caveat-Bold.ttf",
        "resources/fonts/Caveat-Regular.ttf",
        "fonts/Caveat-Bold.ttf",
        "fonts/Caveat-Regular.ttf",
    };
    for (const char* leaf : leaves)
    {
        std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, leaf);
        if (!resourceDir.empty())
        {
            std::string stripped = leaf;
            if (std::string(leaf).rfind("resources/", 0) == 0)
                stripped = std::string(leaf).substr(10);
            paths.push_back(pathJoin(resourceDir, stripped));
            paths.push_back(pathJoin(resourceDir, leaf));
        }
        for (const std::string& path : paths)
        {
            if (!FileExists(path.c_str()))
                continue;
            Font f = LoadFontEx(path.c_str(), 64, nullptr, 0);
            if (f.texture.id != 0)
            {
                SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                return f;
            }
        }
    }
    return GetFontDefault();
}

Rectangle computeParchmentRect(int screenW, int screenH)
{
    // ~8x10 portrait plate centered; leave desk visible on sides and top/bottom.
    const float maxH = screenH * 0.62f;
    const float maxW = screenW * 0.42f;
    float h = maxH;
    float w = h * (8.0f / 10.0f);
    if (w > maxW)
    {
        w = maxW;
        h = w * (10.0f / 8.0f);
    }
    return {
        (screenW - w) * 0.5f,
        screenH * 0.14f,
        w,
        h};
}

} // namespace

void FullscreenParchmentEditor::loadAssets(
    const std::string& resourceDir,
    const std::string& assetRoot)
{
    if (!deskLoaded)
    {
        Texture2D tex{};
        if (loadTextureFromRoots(
                "resources/ui/editor/writing_desk_parchment.png",
                resourceDir,
                assetRoot,
                tex))
        {
            deskTexture = tex;
            deskLoaded = true;
        }
    }
    if (!scriptLoaded)
    {
        scriptFont = tryLoadScriptFont(resourceDir, assetRoot);
        scriptLoaded = (scriptFont.texture.id != 0);
    }
}

void FullscreenParchmentEditor::unloadAssets()
{
    if (deskLoaded && deskTexture.id != 0)
    {
        UnloadTexture(deskTexture);
        deskTexture = {};
        deskLoaded = false;
    }
    if (scriptLoaded && scriptFont.texture.id != 0
        && scriptFont.texture.id != GetFontDefault().texture.id)
    {
        UnloadFont(scriptFont);
        scriptFont = {};
        scriptLoaded = false;
    }
}

void FullscreenParchmentEditor::openEditor(
    std::string* target,
    bool ttsHighlight,
    const std::string& label,
    const std::string& resourceDir,
    const std::string& assetRoot)
{
    if (target == nullptr)
        return;
    loadAssets(resourceDir, assetRoot);
    if (!resourceDir.empty())
        ensureTtsSyntaxThemeLoaded(resourceDir);
    bindTarget = target;
    draft = *target;
    highlightTts = ttsHighlight;
    hintLabel = label;
    cursor = static_cast<int>(draft.size());
    selectAnchor = -1;
    scrollY = 0.0f;
    preferX = -1.0f;
    open = true;
    ignoreInputFrames = 2;
    TraceLog(
        LOG_INFO,
        "TIMBERLINE: parchment editor open (%s) desk=%s script=%s",
        label.c_str(),
        deskLoaded ? "yes" : "no",
        scriptLoaded ? "yes" : "no");
}

void FullscreenParchmentEditor::confirm()
{
    if (bindTarget != nullptr)
        *bindTarget = draft;
    open = false;
    bindTarget = nullptr;
    if (onClosed)
        onClosed();
}

void FullscreenParchmentEditor::cancel()
{
    open = false;
    bindTarget = nullptr;
    draft.clear();
    if (onClosed)
        onClosed();
}

void FullscreenParchmentEditor::typeIntoDraft()
{
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            if (selectAnchor >= 0 && selectAnchor != cursor)
            {
                const int a = std::min(selectAnchor, cursor);
                const int b = std::max(selectAnchor, cursor);
                draft.erase(static_cast<size_t>(a), static_cast<size_t>(b - a));
                cursor = a;
                selectAnchor = -1;
            }
            const std::string clipStr = clip;
            draft.insert(static_cast<size_t>(std::clamp(cursor, 0, static_cast<int>(draft.size()))), clipStr);
            cursor += static_cast<int>(clipStr.size());
        }
        while (GetCharPressed() > 0)
        {
        }
        return;
    }
    if (mod && IsKeyPressed(KEY_A))
    {
        selectAnchor = 0;
        cursor = static_cast<int>(draft.size());
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp == '\n' || cp == '\r')
        {
            draft.insert(static_cast<size_t>(std::clamp(cursor, 0, static_cast<int>(draft.size()))), "\n");
            ++cursor;
        }
        else if (cp >= 32)
        {
            std::string ch;
            insertUtf8(ch, cp);
            draft.insert(static_cast<size_t>(std::clamp(cursor, 0, static_cast<int>(draft.size()))), ch);
            cursor += static_cast<int>(ch.size());
        }
        selectAnchor = -1;
        cp = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        if (selectAnchor >= 0 && selectAnchor != cursor)
        {
            const int a = std::min(selectAnchor, cursor);
            const int b = std::max(selectAnchor, cursor);
            draft.erase(static_cast<size_t>(a), static_cast<size_t>(b - a));
            cursor = a;
            selectAnchor = -1;
        }
        else if (cursor > 0)
        {
            // Delete one UTF-8 codepoint before cursor.
            int i = cursor - 1;
            while (i > 0
                   && (static_cast<unsigned char>(draft[static_cast<size_t>(i)]) & 0xC0)
                       == 0x80)
                --i;
            draft.erase(static_cast<size_t>(i), static_cast<size_t>(cursor - i));
            cursor = i;
        }
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        if (!(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)))
        {
            // Char path may already insert; ensure newline if needed.
        }
    }
}

void FullscreenParchmentEditor::layoutChrome(int screenW, int screenH)
{
    const Rectangle parchment = computeParchmentRect(screenW, screenH);
    lastParchment = parchment;
    lastTextArea = {
        parchment.x + kParchmentMarginX,
        parchment.y + kParchmentMarginY,
        parchment.width - kParchmentMarginX * 2.0f - 14.0f,
        parchment.height - kParchmentMarginY * 2.0f};
    const float btnW = 140.0f;
    const float btnH = 36.0f;
    const float btnY = parchment.y + parchment.height + 24.0f;
    confirmBtn = {parchment.x + parchment.width * 0.5f - btnW - 12.0f, btnY, btnW, btnH};
    cancelBtn = {parchment.x + parchment.width * 0.5f + 12.0f, btnY, btnW, btnH};
}

void FullscreenParchmentEditor::handleInput(int screenW, int screenH)
{
    if (!open)
        return;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }

    typeIntoDraft();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        cancel();
        return;
    }

    const Vector2 mouse = GetMousePosition();
    const bool canClick = editorMousePressed(MOUSE_BUTTON_LEFT);
    const Font font = scriptLoaded ? scriptFont : GetFontDefault();
    layoutChrome(screenW, screenH);

    const float lineH = kScriptFontSize + kLineGap;
    const auto lines = layoutWrappedTextLines(font, draft, lastTextArea.width, kScriptFontSize);
    const float contentH = std::max(lineH, static_cast<float>(std::max<size_t>(1, lines.size())) * lineH);
    const float maxScroll = std::max(0.0f, contentH - lastTextArea.height);
    if (CheckCollisionPointRec(mouse, lastTextArea)
        || CheckCollisionPointRec(mouse, lastParchment))
        scrollY -= GetMouseWheelMove() * lineH * 2.0f;
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    // Caret navigation
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    auto setCursor = [&](int pos) {
        pos = std::clamp(pos, 0, static_cast<int>(draft.size()));
        if (shift)
        {
            if (selectAnchor < 0)
                selectAnchor = cursor;
        }
        else
            selectAnchor = -1;
        cursor = pos;
    };

    const bool left = IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT);
    const bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT);
    const bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
    const bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
    if (left || right || IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_END))
        preferX = -1.0f;
    if (left)
        setCursor(cursor - 1);
    if (right)
        setCursor(cursor + 1);
    if (IsKeyPressed(KEY_HOME))
        setCursor(0);
    if (IsKeyPressed(KEY_END))
        setCursor(static_cast<int>(draft.size()));
    if (up || down)
    {
        const int next = moveCursorVertical(
            font, lines, draft, cursor, up ? -1 : 1, kScriptFontSize, preferX);
        setCursor(next);
        // Keep caret visible.
        const int lineIndex =
            visualLineIndexForCursor(lines, cursor, static_cast<int>(draft.size()));
        const float caretY = static_cast<float>(lineIndex) * lineH;
        if (caretY < scrollY)
            scrollY = caretY;
        if (caretY + lineH > scrollY + lastTextArea.height)
            scrollY = caretY + lineH - lastTextArea.height;
        scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    }

    // Clipboard: Ctrl/Cmd+C / X / V (V also handled in typeIntoDraft).
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X)))
    {
        if (selectAnchor >= 0 && selectAnchor != cursor)
        {
            const int a = std::min(selectAnchor, cursor);
            const int b = std::max(selectAnchor, cursor);
            const std::string selected = draft.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
            SetClipboardText(selected.c_str());
            if (IsKeyPressed(KEY_X))
            {
                draft.erase(static_cast<size_t>(a), static_cast<size_t>(b - a));
                cursor = a;
                selectAnchor = -1;
            }
        }
    }

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, confirmBtn))
        {
            confirm();
            return;
        }
        if (CheckCollisionPointRec(mouse, cancelBtn))
        {
            cancel();
            return;
        }
        if (CheckCollisionPointRec(mouse, lastTextArea))
        {
            // Click-to-place caret: approximate by line + x.
            const float localY = mouse.y - lastTextArea.y + scrollY;
            int lineIdx = static_cast<int>(localY / lineH);
            lineIdx = std::clamp(lineIdx, 0, std::max(0, static_cast<int>(lines.size()) - 1));
            if (!lines.empty())
            {
                const auto& line = lines[static_cast<size_t>(lineIdx)];
                float x = lastTextArea.x;
                int best = line.start;
                for (int i = line.start; i <= line.end; ++i)
                {
                    const std::string prefix = draft.substr(
                        static_cast<size_t>(line.start),
                        static_cast<size_t>(std::max(0, i - line.start)));
                    const float w = MeasureTextEx(font, prefix.c_str(), kScriptFontSize, 1.0f).x;
                    if (lastTextArea.x + w <= mouse.x)
                        best = i;
                    else
                        break;
                    (void)x;
                }
                setCursor(best);
            }
        }
    }
}

void FullscreenParchmentEditor::draw(int screenW, int screenH)
{
    if (!open)
        return;

    // Cover entire editor chrome (must run inside BeginDrawing/EndDrawing).
    DrawRectangle(0, 0, screenW, screenH, Color{10, 8, 6, 255});
    if (deskLoaded && deskTexture.id != 0)
    {
        DrawTexturePro(
            deskTexture,
            {0, 0, static_cast<float>(deskTexture.width), static_cast<float>(deskTexture.height)},
            {0, 0, static_cast<float>(screenW), static_cast<float>(screenH)},
            {0, 0},
            0.0f,
            WHITE);
    }

    const Font font = scriptLoaded ? scriptFont : GetFontDefault();
    layoutChrome(screenW, screenH);
    const Rectangle parchment = lastParchment;

    // Light lift only — realistic lantern falloff is baked into the desk plate.
    // Avoid concentric DrawCircle glows (they read as hard layered rings).
    DrawRectangleRec(
        {parchment.x + 6.0f, parchment.y + 6.0f, parchment.width - 12.0f, parchment.height - 12.0f},
        Color{255, 236, 200, 28});

    const float lineH = kScriptFontSize + kLineGap;
    const auto lines = layoutWrappedTextLines(font, draft, lastTextArea.width, kScriptFontSize);
    const float contentH = std::max(lineH, static_cast<float>(std::max<size_t>(1, lines.size())) * lineH);
    const float maxScroll = std::max(0.0f, contentH - lastTextArea.height);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    // Parchment-tuned palette: theme colors are for dark editor panes.
    const Color ink = Color{32, 24, 14, 255};
    const Color styleTagInk = Color{120, 55, 18, 255};   // <soft>…</soft>
    const Color styleBodyInk = Color{28, 48, 110, 255};  // text inside style wraps
    const Color commandInk = Color{20, 110, 55, 255};    // [pause]
    const Color voiceTagInk = Color{130, 95, 20, 255};   // {{voice:…}}
    const Color voiceBodyInk = Color{20, 110, 55, 255};  // spoken voice spans
    const Color errorInk = Color{150, 30, 30, 255};

    std::vector<Color> ttsColors;
    if (highlightTts && !draft.empty())
    {
        std::vector<timberline_engine::TtsHighlightKind> kinds;
        timberline_engine::classifyTtsTextHighlight(draft, kinds);
        ttsColors.resize(kinds.size(), ink);
        for (size_t i = 0; i < kinds.size(); ++i)
        {
            using timberline_engine::TtsHighlightKind;
            switch (kinds[i])
            {
            case TtsHighlightKind::Command:
                ttsColors[i] = commandInk;
                break;
            case TtsHighlightKind::StyleMarkup:
                ttsColors[i] = styleTagInk;
                break;
            case TtsHighlightKind::StyleContent:
                ttsColors[i] = styleBodyInk;
                break;
            case TtsHighlightKind::VoiceMarkup:
                ttsColors[i] = voiceTagInk;
                break;
            case TtsHighlightKind::VoiceDialog:
                ttsColors[i] = voiceBodyInk;
                break;
            case TtsHighlightKind::MarkupError:
                ttsColors[i] = errorInk;
                break;
            case TtsHighlightKind::Default:
            default:
                ttsColors[i] = ink;
                break;
            }
        }
    }

    BeginScissorMode(
        static_cast<int>(lastTextArea.x),
        static_cast<int>(lastTextArea.y),
        static_cast<int>(lastTextArea.width),
        static_cast<int>(lastTextArea.height));

    const int selA = (selectAnchor >= 0) ? std::min(selectAnchor, cursor) : -1;
    const int selB = (selectAnchor >= 0) ? std::max(selectAnchor, cursor) : -1;

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const float y = lastTextArea.y + static_cast<float>(i) * lineH - scrollY;
        if (y + lineH < lastTextArea.y || y > lastTextArea.y + lastTextArea.height)
            continue;

        if (selA >= 0 && selA < selB)
        {
            const int a = std::max(selA, lines[i].start);
            const int b = std::min(selB, lines[i].end);
            if (a < b)
            {
                const std::string pre = draft.substr(
                    static_cast<size_t>(lines[i].start),
                    static_cast<size_t>(a - lines[i].start));
                const std::string mid = draft.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
                const float x0 = lastTextArea.x
                    + MeasureTextEx(font, pre.c_str(), kScriptFontSize, 1.0f).x;
                const float w = MeasureTextEx(font, mid.c_str(), kScriptFontSize, 1.0f).x;
                DrawRectangleRec(
                    {x0, y, std::max(2.0f, w), kScriptFontSize + 2.0f},
                    Color{180, 150, 90, 120});
            }
        }

        if (lines[i].text.empty())
            continue;

        if (!highlightTts || ttsColors.empty())
        {
            DrawTextEx(
                font,
                lines[i].text.c_str(),
                {lastTextArea.x, y},
                kScriptFontSize,
                1.0f,
                ink);
            continue;
        }

        float drawX = lastTextArea.x;
        size_t ci = 0;
        while (ci < lines[i].text.size())
        {
            const int bufIdx = lines[i].start + static_cast<int>(ci);
            const Color runColor =
                (bufIdx >= 0 && bufIdx < static_cast<int>(ttsColors.size()))
                    ? ttsColors[static_cast<size_t>(bufIdx)]
                    : ink;
            size_t cj = ci + 1;
            while (cj < lines[i].text.size())
            {
                const int jIdx = lines[i].start + static_cast<int>(cj);
                const Color c =
                    (jIdx >= 0 && jIdx < static_cast<int>(ttsColors.size()))
                        ? ttsColors[static_cast<size_t>(jIdx)]
                        : Color{45, 32, 18, 255};
                if (c.r != runColor.r || c.g != runColor.g || c.b != runColor.b
                    || c.a != runColor.a)
                    break;
                ++cj;
            }
            const std::string run = lines[i].text.substr(ci, cj - ci);
            DrawTextEx(font, run.c_str(), {drawX, y}, kScriptFontSize, 1.0f, runColor);
            drawX += MeasureTextEx(font, run.c_str(), kScriptFontSize, 1.0f).x;
            ci = cj;
        }
    }

    // Caret
    {
        int lineIdx = 0;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (cursor >= lines[i].start && cursor <= lines[i].end)
            {
                lineIdx = static_cast<int>(i);
                break;
            }
            if (cursor > lines[i].end)
                lineIdx = static_cast<int>(i);
        }
        if (!lines.empty())
        {
            const auto& line = lines[static_cast<size_t>(lineIdx)];
            const std::string pre = draft.substr(
                static_cast<size_t>(line.start),
                static_cast<size_t>(std::max(0, cursor - line.start)));
            const float cx = lastTextArea.x
                + MeasureTextEx(font, pre.c_str(), kScriptFontSize, 1.0f).x;
            const float cy = lastTextArea.y + static_cast<float>(lineIdx) * lineH - scrollY;
            if (((static_cast<int>(GetTime() * 2.0) % 2) == 0))
                DrawRectangleRec({cx, cy, 2.0f, kScriptFontSize}, Color{60, 40, 20, 220});
        }
    }
    EndScissorMode();

    // Scrollbar in right parchment margin
    if (maxScroll > 1.0f)
    {
        const Rectangle track = {
            parchment.x + parchment.width - kParchmentMarginX + 2.0f,
            lastTextArea.y,
            6.0f,
            lastTextArea.height};
        DrawRectangleRec(track, Color{120, 90, 50, 80});
        const float thumbH = std::max(24.0f, track.height * (lastTextArea.height / contentH));
        const float thumbY =
            track.y + (track.height - thumbH) * (scrollY / maxScroll);
        DrawRectangleRec({track.x, thumbY, track.width, thumbH}, Color{140, 100, 50, 200});
    }

    const Font ui = GetFontDefault();
    drawEditorButton(ui, confirmBtn, "Confirm", true, true);
    drawEditorButton(ui, cancelBtn, "Cancel", false, true);
}

} // namespace timberline_editor
