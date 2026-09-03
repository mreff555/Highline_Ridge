/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "EditorPreferencesDialog.h"
#include "EditorInput.h"
#include "EditorPrefs.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace timberline_editor
{

namespace
{

enum FocusId
{
    kFocusNone = -1,
    kFocusStyle = 0,
    kFocusWorkingSize = 1,
    kFocusWorkingRevs = 2,
    kFocusWorkingSpinner = 3,
    kFocusWorkingTitle = 4,
    kFocusCaretHz = 5,
    kFocusTfPadX = 6,
    kFocusTfPadY = 7,
    kFocusTfGutter = 8,
    kFocusBtnMinW = 9,
    kFocusBtnMaxW = 10,
    kFocusBtnMinH = 11,
    kFocusBtnMaxH = 12,
    kFocusBtnPadX = 13,
    kFocusBtnPadY = 14,
    kFocusBtnFont = 15,
    kFocusBtnLine = 16,
    // 17 = wordWrap toggle (no text focus)
    kFocusSliceL = 18,
    kFocusSliceT = 19,
    kFocusSliceR = 20,
    kFocusSliceB = 21,
    kFocusRaised = 22,
    kFocusDepressed = 23,
    kFocusLabelOffX = 24,
    kFocusLabelOffY = 25,
    kFocusMapDragPanSpeed = 26,
};

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

void backspaceAt(std::string& buffer, int& caret)
{
    if (caret <= 0 || buffer.empty())
        return;
    int i = caret - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i), static_cast<size_t>(caret - i));
    caret = i;
}

std::string fmtFloat(float v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

std::string fmtInt(int v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

bool parseFloat(const std::string& s, float& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str())
        return false;
    out = v;
    return true;
}

bool parseInt(const std::string& s, int& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str())
        return false;
    out = static_cast<int>(v);
    return true;
}

float drawLabeledField(
    Font font,
    float x,
    float y,
    float labelW,
    float fieldW,
    float fieldH,
    const char* label,
    const std::string& value,
    int fieldId,
    int focusField,
    bool canClick,
    Vector2 mouse)
{
    DrawTextEx(font, label, {x, y + 6.0f}, kFontTiny, 1.0f, kTextMuted);
    const Rectangle box = {x + labelW, y, fieldW, fieldH};
    DrawRectangleRec(box, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(
        box, 1.0f, focusField == fieldId ? kPanelBorder : kPanelInnerEdge);

    const std::string shown =
        value.size() > 80 ? value.substr(0, 77) + "…" : value;
    DrawTextEx(
        font,
        shown.c_str(),
        {box.x + 8.0f, box.y + (fieldH - kFontSmall) * 0.5f},
        kFontSmall,
        1.0f,
        kTextPrimary);

    if (focusField == fieldId && caretBlinkVisible(editorButtons().caretBlinkHz))
    {
        const float tw =
            MeasureTextEx(font, shown.c_str(), kFontSmall, 1.0f).x;
        DrawRectangle(
            static_cast<int>(box.x + 8.0f + tw + 1.0f),
            static_cast<int>(box.y + 4.0f),
            2,
            static_cast<int>(fieldH - 8.0f),
            kPanelBorder);
    }

    if (canClick && CheckCollisionPointRec(mouse, box))
        return static_cast<float>(fieldId); // signal: caller sets focus
    return -1.0f;
}

} // namespace

void EditorPreferencesDialog::snapshotFromLive()
{
    const EditorButtonResources& res = editorButtons();
    working = res.working;
    caretBlinkHz = res.caretBlinkHz;
    textFieldPadX = res.textFieldPadX;
    textFieldPadY = res.textFieldPadY;
    textFieldScrollGutter = res.textFieldScrollGutter;
    buttons = res.config;
}

std::string EditorPreferencesDialog::draftForField(int fieldId) const
{
    switch (fieldId)
    {
    case kFocusWorkingSize:
        return fmtInt(working.sizePx);
    case kFocusWorkingRevs:
        return fmtFloat(working.revolutionsPerSecond);
    case kFocusWorkingSpinner:
        return working.spinnerPath;
    case kFocusWorkingTitle:
        return working.title;
    case kFocusCaretHz:
        return fmtFloat(caretBlinkHz);
    case kFocusTfPadX:
        return fmtFloat(textFieldPadX);
    case kFocusTfPadY:
        return fmtFloat(textFieldPadY);
    case kFocusTfGutter:
        return fmtFloat(textFieldScrollGutter);
    case kFocusBtnMinW:
        return fmtFloat(buttons.minWidth);
    case kFocusBtnMaxW:
        return fmtFloat(buttons.maxWidth);
    case kFocusBtnMinH:
        return fmtFloat(buttons.minHeight);
    case kFocusBtnMaxH:
        return fmtFloat(buttons.maxHeight);
    case kFocusBtnPadX:
        return fmtFloat(buttons.padX);
    case kFocusBtnPadY:
        return fmtFloat(buttons.padY);
    case kFocusBtnFont:
        return fmtFloat(buttons.fontSize);
    case kFocusBtnLine:
        return fmtFloat(buttons.lineSpacing);
    case kFocusSliceL:
        return fmtInt(buttons.sliceLeft);
    case kFocusSliceT:
        return fmtInt(buttons.sliceTop);
    case kFocusSliceR:
        return fmtInt(buttons.sliceRight);
    case kFocusSliceB:
        return fmtInt(buttons.sliceBottom);
    case kFocusRaised:
        return buttons.raisedSkinPath;
    case kFocusDepressed:
        return buttons.depressedSkinPath;
    case kFocusLabelOffX:
        return fmtFloat(buttons.labelOffsetPressedX);
    case kFocusLabelOffY:
        return fmtFloat(buttons.labelOffsetPressedY);
    case kFocusMapDragPanSpeed:
        return fmtFloat(mapDragPanSpeed);
    default:
        return {};
    }
}

void EditorPreferencesDialog::commitFocusDraft()
{
    if (focusField <= kFocusStyle)
        return;

    int iv = 0;
    float fv = 0.0f;
    switch (focusField)
    {
    case kFocusWorkingSpinner:
        working.spinnerPath = focusDraft;
        break;
    case kFocusWorkingTitle:
        working.title = focusDraft;
        break;
    case kFocusRaised:
        buttons.raisedSkinPath = focusDraft;
        break;
    case kFocusDepressed:
        buttons.depressedSkinPath = focusDraft;
        break;
    case kFocusWorkingSize:
        if (parseInt(focusDraft, iv))
            working.sizePx = std::clamp(iv, 16, 512);
        break;
    case kFocusSliceL:
        if (parseInt(focusDraft, iv))
            buttons.sliceLeft = std::max(0, iv);
        break;
    case kFocusSliceT:
        if (parseInt(focusDraft, iv))
            buttons.sliceTop = std::max(0, iv);
        break;
    case kFocusSliceR:
        if (parseInt(focusDraft, iv))
            buttons.sliceRight = std::max(0, iv);
        break;
    case kFocusSliceB:
        if (parseInt(focusDraft, iv))
            buttons.sliceBottom = std::max(0, iv);
        break;
    case kFocusWorkingRevs:
        if (parseFloat(focusDraft, fv))
            working.revolutionsPerSecond = std::clamp(fv, 0.05f, 8.0f);
        break;
    case kFocusCaretHz:
        if (parseFloat(focusDraft, fv))
            caretBlinkHz = std::clamp(fv, 0.2f, 8.0f);
        break;
    case kFocusTfPadX:
        if (parseFloat(focusDraft, fv))
            textFieldPadX = fv;
        break;
    case kFocusTfPadY:
        if (parseFloat(focusDraft, fv))
            textFieldPadY = fv;
        break;
    case kFocusTfGutter:
        if (parseFloat(focusDraft, fv))
            textFieldScrollGutter = fv;
        break;
    case kFocusBtnMinW:
        if (parseFloat(focusDraft, fv))
            buttons.minWidth = fv;
        break;
    case kFocusBtnMaxW:
        if (parseFloat(focusDraft, fv))
            buttons.maxWidth = fv;
        break;
    case kFocusBtnMinH:
        if (parseFloat(focusDraft, fv))
            buttons.minHeight = fv;
        break;
    case kFocusBtnMaxH:
        if (parseFloat(focusDraft, fv))
            buttons.maxHeight = fv;
        break;
    case kFocusBtnPadX:
        if (parseFloat(focusDraft, fv))
            buttons.padX = fv;
        break;
    case kFocusBtnPadY:
        if (parseFloat(focusDraft, fv))
            buttons.padY = fv;
        break;
    case kFocusBtnFont:
        if (parseFloat(focusDraft, fv))
            buttons.fontSize = fv;
        break;
    case kFocusBtnLine:
        if (parseFloat(focusDraft, fv))
            buttons.lineSpacing = fv;
        break;
    case kFocusLabelOffX:
        if (parseFloat(focusDraft, fv))
            buttons.labelOffsetPressedX = fv;
        break;
    case kFocusLabelOffY:
        if (parseFloat(focusDraft, fv))
            buttons.labelOffsetPressedY = fv;
        break;
    case kFocusMapDragPanSpeed:
        if (parseFloat(focusDraft, fv))
            mapDragPanSpeed = std::clamp(fv, 0.0f, 2000.0f);
        break;
    default:
        break;
    }
}

void EditorPreferencesDialog::setFocus(int fieldId)
{
    if (fieldId == focusField)
        return;
    commitFocusDraft();
    focusField = fieldId;
    if (fieldId == kFocusStyle)
    {
        focusDraft.clear();
        styleCaret = static_cast<int>(styleFilter.size());
    }
    else if (fieldId > kFocusStyle)
        focusDraft = draftForField(fieldId);
    else
        focusDraft.clear();
}

void EditorPreferencesDialog::openDialog(
    const std::string& resourceDirIn,
    const std::string& assetRootIn)
{
    resourceDir = resourceDirIn;
    assetRoot = assetRootIn;
    styleFilter = loadGenerationStyleFilter(resourceDir);
    mapDragPanSpeed = loadMapDragPanSpeed(resourceDir);
    styleCaret = static_cast<int>(styleFilter.size());
    snapshotFromLive();
    status.clear();
    error.clear();
    focusField = kFocusNone;
    focusDraft.clear();
    setFocus(kFocusStyle);
    scrollY = 0.0f;
    lastContentHeight = 0.0f;
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
}

void EditorPreferencesDialog::closeDialog()
{
    open = false;
    focusField = kFocusNone;
    focusDraft.clear();
    error.clear();
}

bool EditorPreferencesDialog::applyAndSave()
{
    commitFocusDraft();
    error.clear();
    status.clear();

    if (resourceDir.empty())
    {
        error = "No resource directory";
        return false;
    }

    if (!saveGenerationStyleFilter(resourceDir, styleFilter))
    {
        error = "Failed to save editor_prefs.json";
        return false;
    }
    if (!saveMapDragPanSpeed(resourceDir, mapDragPanSpeed))
    {
        error = "Failed to save map drag/pan speed";
        return false;
    }
    mapDragPanSpeed = loadMapDragPanSpeed(resourceDir);

    EditorButtonResources& res = editorButtons();
    res.working = working;
    res.caretBlinkHz = caretBlinkHz;
    if (res.caretBlinkHz < 0.2f)
        res.caretBlinkHz = 0.2f;
    if (res.caretBlinkHz > 8.0f)
        res.caretBlinkHz = 8.0f;
    res.textFieldPadX = textFieldPadX;
    res.textFieldPadY = textFieldPadY;
    res.textFieldScrollGutter = textFieldScrollGutter;
    res.config = buttons;

    if (!saveEditorUiConfig(resourceDir, res))
    {
        error = "Failed to write editor_ui_config.json";
        return false;
    }

    // Hot-reload skins / spinner / clamps from disk.
    res.load(resourceDir, assetRoot);
    snapshotFromLive();
    if (focusField > kFocusStyle)
        focusDraft = draftForField(focusField);
    status = "Saved";
    return true;
}

void EditorPreferencesDialog::typeIntoFocusedField()
{
    if (focusField < 0)
        return;

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    const bool multiline = (focusField == kFocusStyle);
    std::string* target = multiline ? &styleFilter : &focusDraft;
    const bool isInt = focusField == kFocusWorkingSize || focusField == kFocusSliceL
        || focusField == kFocusSliceT || focusField == kFocusSliceR
        || focusField == kFocusSliceB;
    const bool isFloat = !multiline && !isInt
        && focusField != kFocusWorkingSpinner && focusField != kFocusWorkingTitle
        && focusField != kFocusRaised && focusField != kFocusDepressed;

    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            if (multiline)
            {
                styleFilter.insert(static_cast<size_t>(styleCaret), clip);
                styleCaret += static_cast<int>(std::strlen(clip));
            }
            else
                focusDraft += clip;
        }
        while (GetCharPressed() > 0)
        {
        }
    }
    else
    {
        int cp = GetCharPressed();
        while (cp > 0)
        {
            if (multiline)
            {
                if (cp >= 32)
                {
                    std::string piece;
                    insertUtf8(piece, cp);
                    styleFilter.insert(static_cast<size_t>(styleCaret), piece);
                    styleCaret += static_cast<int>(piece.size());
                }
            }
            else if (cp >= 32 && cp != '\n')
            {
                if (isInt)
                {
                    if ((cp >= '0' && cp <= '9') || cp == '-')
                        insertUtf8(focusDraft, cp);
                }
                else if (isFloat)
                {
                    if ((cp >= '0' && cp <= '9') || cp == '-' || cp == '.' || cp == 'e'
                        || cp == 'E' || cp == '+')
                        insertUtf8(focusDraft, cp);
                }
                else
                    insertUtf8(focusDraft, cp);
            }
            cp = GetCharPressed();
        }
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        if (multiline)
            backspaceAt(styleFilter, styleCaret);
        else if (!focusDraft.empty())
        {
            int end = static_cast<int>(focusDraft.size());
            backspaceAt(focusDraft, end);
        }
    }

    if (IsKeyPressed(KEY_ENTER) && multiline)
    {
        styleFilter.insert(static_cast<size_t>(styleCaret), "\n");
        styleCaret += 1;
    }

    if (multiline)
        styleCaret = std::clamp(styleCaret, 0, static_cast<int>(styleFilter.size()));

    (void)target;
}

void EditorPreferencesDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;

    if (waitMouseRelease)
    {
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }

    typeIntoFocusedField();

    if (IsKeyPressed(KEY_ESCAPE))
        closeDialog();
}

void EditorPreferencesDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(780.0f, screenW - 40.0f);
    const float dialogH = std::min(700.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Editor Preferences",
        {dialog.x + 20.0f, dialog.y + 14.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        "Generation style + UI metrics (editor_ui_config.json)",
        {dialog.x + 20.0f, dialog.y + 42.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float pad = 16.0f;
    const float footerH = 56.0f;
    const Rectangle content = {
        dialog.x + pad,
        dialog.y + 68.0f,
        dialog.width - pad * 2.0f,
        dialogH - 68.0f - footerH};
    DrawRectangleRec(content, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(content, 1.0f, kPanelInnerEdge);

    // Matching inner margins inside the content border (top == bottom).
    const float padInner = 10.0f;
    // Scissor inset so glyphs don't sit under / bleed past the border stroke.
    const float clipInset = 2.0f;
    const Rectangle clip = {
        content.x + clipInset,
        content.y + clipInset,
        content.width - clipInset * 2.0f,
        content.height - clipInset * 2.0f};

    // Prefer measured height after layout; seed so first frame can scroll.
    if (lastContentHeight < 1.0f)
        lastContentHeight = 980.0f;
    const float maxScroll = std::max(0.0f, lastContentHeight - content.height);
    if (CheckCollisionPointRec(mouse, content))
        scrollY -= GetMouseWheelMove() * 28.0f;
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    // Single scissor for the whole form — do not call helpers that Begin/EndScissor
    // (raylib scissor is not nested; EndScissor clears the parent clip).
    BeginScissorMode(
        static_cast<int>(clip.x),
        static_cast<int>(clip.y),
        static_cast<int>(clip.width),
        static_cast<int>(clip.height));

    float y = content.y + padInner - scrollY;
    const float x = content.x + 12.0f;
    const float w = content.width - 24.0f;
    const float labelW = 150.0f;
    const float fieldH = 28.0f;
    const float rowGap = 34.0f;

    auto tryFocus = [&](float signal)
    {
        // Ignore hits on rows scrolled outside the visible content pane.
        if (signal >= 0.0f && CheckCollisionPointRec(mouse, content))
            setFocus(static_cast<int>(signal));
    };

    auto fieldValue = [&](int fieldId) -> std::string
    {
        if (focusField == fieldId)
            return focusDraft;
        return draftForField(fieldId);
    };

    // --- Generation style filter ---
    DrawTextEx(font, "Generation style filter", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 22.0f;
    DrawTextEx(
        font,
        "Semicolon-separated clauses (saved to editor_prefs.json)",
        {x, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 18.0f;

    const float styleH = 110.0f;
    const Rectangle styleBox = {x, y, w, styleH};
    DrawRectangleRec(styleBox, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(
        styleBox, 1.0f, focusField == kFocusStyle ? kPanelBorder : kPanelInnerEdge);

    {
        const float stylePad = 8.0f;
        const float lineH = kFontSmall + 4.0f;
        const auto lines = layoutWrappedTextLines(
            font, styleFilter, w - stylePad * 2.0f, kFontSmall);
        // Draw under the parent content scissor only (no nested scissor).
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const float ly = styleBox.y + stylePad + static_cast<float>(i) * lineH;
            if (ly + lineH < clip.y || ly > clip.y + clip.height)
                continue;
            if (lines[i].text.empty())
                continue;
            DrawTextEx(
                font,
                lines[i].text.c_str(),
                {styleBox.x + stylePad, ly},
                kFontSmall,
                1.0f,
                kTextPrimary);
        }
        if (focusField == kFocusStyle && caretBlinkVisible(editorButtons().caretBlinkHz))
        {
            const int li = visualLineIndexForCursor(
                lines, styleCaret, static_cast<int>(styleFilter.size()));
            if (li >= 0 && li < static_cast<int>(lines.size()))
            {
                const float cx = styleBox.x + stylePad
                    + caretXOnVisualLine(
                        font, lines[static_cast<size_t>(li)], styleCaret, kFontSmall);
                const float cy = styleBox.y + stylePad + static_cast<float>(li) * lineH;
                if (cy + kFontSmall >= clip.y && cy <= clip.y + clip.height)
                {
                    DrawRectangle(
                        static_cast<int>(cx),
                        static_cast<int>(cy),
                        2,
                        static_cast<int>(kFontSmall),
                        kPanelBorder);
                }
            }
        }
        if (canClick && CheckCollisionPointRec(mouse, styleBox)
            && CheckCollisionPointRec(mouse, content))
        {
            setFocus(kFocusStyle);
            styleCaret = cursorIndexFromClick(
                font,
                lines,
                styleFilter,
                styleBox,
                stylePad,
                kFontSmall,
                lineH,
                0.0f,
                mouse);
        }
    }
    y += styleH + 16.0f;

    // --- Map ---
    DrawTextEx(font, "Map", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 22.0f;
    DrawTextEx(
        font,
        "Auto-pan speed when dragging near the map edge. 0 = off.",
        {x, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 18.0f;
    tryFocus(drawLabeledField(
        font,
        x,
        y,
        labelW,
        120.0f,
        fieldH,
        "Drag/pan speed",
        fieldValue(kFocusMapDragPanSpeed),
        kFocusMapDragPanSpeed,
        focusField,
        canClick,
        mouse));
    y += rowGap + 8.0f;

    // --- Working overlay ---
    DrawTextEx(font, "Working overlay", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 24.0f;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "sizePx", fieldValue(kFocusWorkingSize),
        kFocusWorkingSize, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "revs/sec", fieldValue(kFocusWorkingRevs),
        kFocusWorkingRevs, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, w - labelW, fieldH, "spinnerPath",
        fieldValue(kFocusWorkingSpinner), kFocusWorkingSpinner, focusField, canClick,
        mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, w - labelW, fieldH, "title", fieldValue(kFocusWorkingTitle),
        kFocusWorkingTitle, focusField, canClick, mouse));
    y += rowGap + 8.0f;

    // --- Text fields ---
    DrawTextEx(font, "Text fields", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 24.0f;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "caretBlinkHz", fieldValue(kFocusCaretHz),
        kFocusCaretHz, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "padX", fieldValue(kFocusTfPadX),
        kFocusTfPadX, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "padY", fieldValue(kFocusTfPadY),
        kFocusTfPadY, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "scrollGutter", fieldValue(kFocusTfGutter),
        kFocusTfGutter, focusField, canClick, mouse));
    y += rowGap + 8.0f;

    // --- Buttons ---
    DrawTextEx(font, "Buttons", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 24.0f;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "minWidth", fieldValue(kFocusBtnMinW),
        kFocusBtnMinW, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "maxWidth", fieldValue(kFocusBtnMaxW),
        kFocusBtnMaxW, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "minHeight", fieldValue(kFocusBtnMinH),
        kFocusBtnMinH, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "maxHeight", fieldValue(kFocusBtnMaxH),
        kFocusBtnMaxH, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "padX", fieldValue(kFocusBtnPadX),
        kFocusBtnPadX, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "padY", fieldValue(kFocusBtnPadY),
        kFocusBtnPadY, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "fontSize", fieldValue(kFocusBtnFont),
        kFocusBtnFont, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "lineSpacing", fieldValue(kFocusBtnLine),
        kFocusBtnLine, focusField, canClick, mouse));
    y += rowGap;

    {
        const Rectangle wrapBtn = {x + labelW, y, 140.0f, fieldH};
        DrawTextEx(font, "wordWrap", {x, y + 6.0f}, kFontTiny, 1.0f, kTextMuted);
        drawEditorButton(
            font,
            wrapBtn,
            buttons.wordWrap ? "wordWrap: ON" : "wordWrap: off",
            buttons.wordWrap,
            true);
        if (canClick && CheckCollisionPointRec(mouse, wrapBtn)
            && CheckCollisionPointRec(mouse, content))
            buttons.wordWrap = !buttons.wordWrap;
    }
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 80.0f, fieldH, "slice L", fieldValue(kFocusSliceL),
        kFocusSliceL, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 80.0f, fieldH, "slice T", fieldValue(kFocusSliceT),
        kFocusSliceT, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 80.0f, fieldH, "slice R", fieldValue(kFocusSliceR),
        kFocusSliceR, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 80.0f, fieldH, "slice B", fieldValue(kFocusSliceB),
        kFocusSliceB, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, w - labelW, fieldH, "raisedSkin", fieldValue(kFocusRaised),
        kFocusRaised, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, w - labelW, fieldH, "depressedSkin",
        fieldValue(kFocusDepressed), kFocusDepressed, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "labelOff X", fieldValue(kFocusLabelOffX),
        kFocusLabelOffX, focusField, canClick, mouse));
    y += rowGap;
    tryFocus(drawLabeledField(
        font, x, y, labelW, 120.0f, fieldH, "labelOff Y", fieldValue(kFocusLabelOffY),
        kFocusLabelOffY, focusField, canClick, mouse));
    y += fieldH;

    // Content height includes matching bottom padInner (same as top).
    lastContentHeight = (y + scrollY) - content.y + padInner;
    EndScissorMode();
    scrollY = std::clamp(scrollY, 0.0f, std::max(0.0f, lastContentHeight - content.height));

    // Footer
    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialog.y + dialogH - btnH - 12.0f;
    const Rectangle saveBtn = {dialog.x + pad, btnY, btnW, btnH};
    const Rectangle cancelBtn = {dialog.x + dialogW - btnW - pad, btnY, btnW, btnH};
    drawEditorButton(font, saveBtn, "Save", true, true);
    drawEditorButton(font, cancelBtn, "Cancel", false, true);
    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, saveBtn))
            applyAndSave();
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog))
            closeDialog();
    }

    if (!status.empty())
        DrawTextEx(
            font,
            status.c_str(),
            {saveBtn.x + btnW + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    if (!error.empty())
        DrawTextEx(
            font,
            error.c_str(),
            {saveBtn.x + btnW + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{220, 100, 90, 255});
}

} // namespace timberline_editor
